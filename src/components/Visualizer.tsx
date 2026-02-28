import { useRef, useEffect } from 'react'
import type React from 'react'
import { getFrequencyData } from '../lib/audioAnalyser'
import { perfMarkStart } from '../lib/perf'
import useVisualSettings from '../hooks/useVisualSettings'
import type { ContourData, VisualizerStyle, SegmentationResult } from '../types'

interface VisualizerProps {
  contourData: ContourData | null
  analyserRef: React.RefObject<AnalyserNode | null>
  dataArrayRef: React.RefObject<Uint8Array | null>
  accentColor: string
  isPlaying: boolean
  segmentation?: SegmentationResult | null
}

interface ParsedColor {
  r: number
  g: number
  b: number
}

interface BarGeometry {
  px: number
  py: number
  ex: number
  ey: number
  alpha: number
}

const FULL_SURFACE_BAR_COUNT = 48

/**
 * Canvas overlay that draws audio-reactive bars.
 * Supports multiple styles: contour-bars, full-surface, depth-contour.
 */
export default function Visualizer({ contourData, analyserRef, dataArrayRef, accentColor, isPlaying, segmentation }: VisualizerProps) {
  const { settings } = useVisualSettings()
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null)
  const smoothedRef = useRef<Float32Array | null>(null)
  const sizeRef = useRef<{ w: number; h: number }>({ w: 0, h: 0 })

  // Refs for values read per-frame inside the render loop.
  // Avoids tearing down/rebuilding the entire effect on slider drags.
  const intensityRef = useRef(settings.visualizerIntensity)
  const colorModeRef = useRef(settings.visualizerColorMode)
  const customColorRef = useRef(settings.visualizerCustomColor)
  const accentColorRef = useRef(accentColor)
  const segmentationRef = useRef(segmentation)

  intensityRef.current = settings.visualizerIntensity
  colorModeRef.current = settings.visualizerColorMode
  customColorRef.current = settings.visualizerCustomColor
  accentColorRef.current = accentColor
  segmentationRef.current = segmentation

  const visualizerStyle: VisualizerStyle = settings.visualizerStyle

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return

    const ctx = canvas.getContext('2d')
    if (!ctx) return

    let resizeRafId: number | null = null

    const resizeCanvas = () => {
      const dpr = window.devicePixelRatio || 1
      const rect = canvas.getBoundingClientRect()
      const w = rect.width
      const h = rect.height
      if (w === 0 || h === 0) return
      canvas.width = w * dpr
      canvas.height = h * dpr
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      sizeRef.current = { w, h }
    }

    // Coalesce rapid ResizeObserver callbacks (e.g. window drag-resize)
    // into one canvas buffer reallocation per animation frame
    const debouncedResize = () => {
      if (resizeRafId !== null) return
      resizeRafId = requestAnimationFrame(() => {
        resizeRafId = null
        resizeCanvas()
      })
    }

    const observer = new ResizeObserver(debouncedResize)
    observer.observe(canvas)
    resizeCanvas()

    // Pre-compute contour info for contour-based styles
    const contours = contourData?.contours || []
    const totalPoints = contours.reduce((sum, c) => sum + c.points.length, 0)

    // Determine smoothed array size based on style
    const smoothedSize = visualizerStyle === 'full-surface'
      ? FULL_SURFACE_BAR_COUNT
      : totalPoints

    if (!smoothedRef.current || smoothedRef.current.length !== smoothedSize) {
      smoothedRef.current = new Float32Array(smoothedSize)
    }

    // Pre-allocated per-frame bar geometry pool — avoids object creation every frame
    const maxBars = Math.max(smoothedSize, FULL_SURFACE_BAR_COUNT)
    const barPool: BarGeometry[] = Array.from({ length: maxBars }, () => ({ px: 0, py: 0, ex: 0, ey: 0, alpha: 0 }))
    let barCount = 0
    // Pre-allocated alpha bucket indices (11 levels: 0.0..1.0 quantized to 0.1)
    const ALPHA_BUCKETS = 11
    const bucketIndices: number[][] = Array.from({ length: ALPHA_BUCKETS }, () => [])

    // Compute per-frame styling from refs (no effect teardown on changes)
    function computeFrameStyle() {
      const intensity = intensityRef.current / 100
      const color = resolveBarColor(colorModeRef.current, customColorRef.current, accentColorRef.current)

      // Saturate: push color channels away from gray (amplify dominant channel)
      const maxC = Math.max(color.r, color.g, color.b, 1)
      const satBoost = 0.3 + intensity * 0.4
      const satR = Math.min(255, color.r + (color.r / maxC) * 255 * satBoost)
      const satG = Math.min(255, color.g + (color.g / maxC) * 255 * satBoost)
      const satB = Math.min(255, color.b + (color.b / maxC) * 255 * satBoost)

      const brighten = 0.15 + intensity * 0.15
      const glowR = Math.round(satR + (255 - satR) * brighten)
      const glowG = Math.round(satG + (255 - satG) * brighten)
      const glowB = Math.round(satB + (255 - satB) * brighten)

      const coreBrighten = 0.3 + intensity * 0.3
      const coreR = Math.round(glowR + (255 - glowR) * coreBrighten)
      const coreG = Math.round(glowG + (255 - glowG) * coreBrighten)
      const coreB = Math.round(glowB + (255 - glowB) * coreBrighten)

      const glowAlphaMul = 0.3 + intensity * 0.7
      const coreAlphaMul = 0.4 + intensity * 0.6

      const hasDepthMask = !!segmentationRef.current
      const padX = hasDepthMask ? 0.06 : 0
      const padBot = hasDepthMask ? 0.04 : 0

      return { glowR, glowG, glowB, coreR, coreG, coreB, glowAlphaMul, coreAlphaMul, padX, padBot }
    }

    function renderContourBars() {
      const { w, h } = sizeRef.current
      ctx!.clearRect(0, 0, w, h)

      const analyser = analyserRef?.current
      const dataArray = dataArrayRef?.current

      if (!analyser || !dataArray || contours.length === 0 || w === 0) return

      const { glowR, glowG, glowB, coreR, coreG, coreB, glowAlphaMul, coreAlphaMul, padX, padBot } = computeFrameStyle()

      getFrequencyData(analyser, dataArray as Uint8Array<ArrayBuffer>)

      const binCount = Math.floor(dataArray.length * 0.93) // trim dead high frequencies
      const smoothed = smoothedRef.current!
      let smoothedOffset = 0

      for (const contour of contours) {
        const { points, span } = contour
        if (!points.length) {
          smoothedOffset += points.length
          continue
        }

        const maxBarLen = 20 + span * 40
        const shadowWidth = 7 + span * 4
        const glowWidth = 4 + span * 3
        const coreWidth = 1.5 + span

        barCount = 0
        for (let b = 0; b < ALPHA_BUCKETS; b++) bucketIndices[b]!.length = 0

        for (let i = 0; i < points.length; i++) {
          const t = i / points.length
          const logIndex = Math.floor(Math.pow(t, 1.5) * (binCount - 1))
          const rawValue = dataArray[logIndex]! / 255

          const si = smoothedOffset + i
          smoothed[si] = smoothed[si]! * 0.4 + rawValue * 0.6
          const value = smoothed[si]!

          if (value < 0.02) continue

          const p = points[i]!
          const px = p.x * w
          const py = p.y * h
          const barLength = value * maxBarLen
          const nx = p.nx
          const ny = p.ny

          // Clamp bar endpoints within inset area
          const minX = w * padX
          const maxX = w - w * padX
          const maxY = h - h * padBot

          let clampedLen = barLength
          if (nx !== 0) {
            const maxLenX = nx > 0 ? (maxX - px) / nx : (minX - px) / nx
            if (maxLenX > 0 && maxLenX < clampedLen) clampedLen = maxLenX
          }
          if (ny !== 0) {
            const maxLenY = ny > 0 ? (maxY - py) / ny : -py / ny
            if (maxLenY > 0 && maxLenY < clampedLen) clampedLen = maxLenY
          }

          const bar = barPool[barCount]!
          bar.px = px
          bar.py = py
          bar.ex = px + nx * clampedLen
          bar.ey = py + ny * clampedLen
          bar.alpha = 0.5 + value * 0.5
          bucketIndices[Math.round(bar.alpha * 10)]!.push(barCount)
          barCount++
        }

        ctx!.lineCap = 'round'

        // Shadow pass — dark outline for contrast against light backgrounds
        ctx!.lineWidth = shadowWidth
        for (let b = 0; b < ALPHA_BUCKETS; b++) {
          const indices = bucketIndices[b]!
          if (indices.length === 0) continue
          ctx!.strokeStyle = `rgba(0, 0, 0, ${(b / 10) * 0.35})`
          ctx!.beginPath()
          for (let j = 0; j < indices.length; j++) {
            const bar = barPool[indices[j]!]!
            ctx!.moveTo(bar.px, bar.py)
            ctx!.lineTo(bar.ex, bar.ey)
          }
          ctx!.stroke()
        }

        ctx!.lineWidth = glowWidth
        for (let b = 0; b < ALPHA_BUCKETS; b++) {
          const indices = bucketIndices[b]!
          if (indices.length === 0) continue
          ctx!.strokeStyle = `rgba(${glowR}, ${glowG}, ${glowB}, ${(b / 10) * glowAlphaMul})`
          ctx!.beginPath()
          for (let j = 0; j < indices.length; j++) {
            const bar = barPool[indices[j]!]!
            ctx!.moveTo(bar.px, bar.py)
            ctx!.lineTo(bar.ex, bar.ey)
          }
          ctx!.stroke()
        }

        ctx!.lineWidth = coreWidth
        for (let b = 0; b < ALPHA_BUCKETS; b++) {
          const indices = bucketIndices[b]!
          if (indices.length === 0) continue
          ctx!.strokeStyle = `rgba(${coreR}, ${coreG}, ${coreB}, ${(b / 10) * coreAlphaMul})`
          ctx!.beginPath()
          for (let j = 0; j < indices.length; j++) {
            const bar = barPool[indices[j]!]!
            ctx!.moveTo(bar.px, bar.py)
            ctx!.lineTo(bar.ex, bar.ey)
          }
          ctx!.stroke()
        }

        smoothedOffset += points.length
      }
    }

    function renderFullSurface() {
      const { w, h } = sizeRef.current
      ctx!.clearRect(0, 0, w, h)

      const analyser = analyserRef?.current
      const dataArray = dataArrayRef?.current

      if (!analyser || !dataArray || w === 0) return

      const { glowR, glowG, glowB, coreR, coreG, coreB, glowAlphaMul, coreAlphaMul, padX, padBot } = computeFrameStyle()

      getFrequencyData(analyser, dataArray as Uint8Array<ArrayBuffer>)

      const binCount = Math.floor(dataArray.length * 0.93) // trim dead high frequencies
      const smoothed = smoothedRef.current!
      const numBars = FULL_SURFACE_BAR_COUNT

      // Compute inset area
      const insetL = w * padX
      const insetR = w * padX
      const insetB = h * padBot
      const areaW = w - insetL - insetR
      const areaH = h - insetB

      const barWidth = areaW / numBars
      const gap = barWidth * 0.15
      const actualBarWidth = barWidth - gap

      barCount = 0
      for (let i = 0; i < numBars; i++) {
        const t = i / numBars
        const logIndex = Math.floor(Math.pow(t, 1.5) * (binCount - 1))
        const rawValue = dataArray[logIndex]! / 255

        smoothed[i] = smoothed[i]! * 0.4 + rawValue * 0.6
        const value = smoothed[i]!

        if (value < 0.02) continue

        const barHeight = value * areaH
        const x = insetL + i * barWidth + gap / 2
        const y = areaH - barHeight

        const bar = barPool[barCount++]!
        bar.px = x
        bar.py = y
        bar.ex = x + actualBarWidth
        bar.ey = areaH
        bar.alpha = 0.4 + value * 0.6
      }

      // Shadow pass
      for (let i = 0; i < barCount; i++) {
        const bar = barPool[i]!
        const gradient = ctx!.createLinearGradient(0, bar.py, 0, bar.ey)
        gradient.addColorStop(0, `rgba(0, 0, 0, ${bar.alpha * 0.35})`)
        gradient.addColorStop(1, 'rgba(0, 0, 0, 0)')
        ctx!.fillStyle = gradient
        ctx!.fillRect(bar.px - 3, bar.py - 3, bar.ex - bar.px + 6, bar.ey - bar.py + 6)
      }

      // Glow pass
      for (let i = 0; i < barCount; i++) {
        const bar = barPool[i]!
        const gradient = ctx!.createLinearGradient(0, bar.py, 0, bar.ey)
        gradient.addColorStop(0, `rgba(${glowR}, ${glowG}, ${glowB}, ${bar.alpha * glowAlphaMul})`)
        gradient.addColorStop(1, `rgba(${glowR}, ${glowG}, ${glowB}, 0)`)
        ctx!.fillStyle = gradient
        ctx!.fillRect(bar.px - 2, bar.py - 2, bar.ex - bar.px + 4, bar.ey - bar.py + 4)
      }

      // Core pass
      for (let i = 0; i < barCount; i++) {
        const bar = barPool[i]!
        const gradient = ctx!.createLinearGradient(0, bar.py, 0, bar.ey)
        gradient.addColorStop(0, `rgba(${coreR}, ${coreG}, ${coreB}, ${bar.alpha * coreAlphaMul})`)
        gradient.addColorStop(0.7, `rgba(${glowR}, ${glowG}, ${glowB}, ${bar.alpha * coreAlphaMul * 0.8})`)
        gradient.addColorStop(1, `rgba(${glowR}, ${glowG}, ${glowB}, 0)`)
        ctx!.fillStyle = gradient
        ctx!.fillRect(bar.px, bar.py, bar.ex - bar.px, bar.ey - bar.py)
      }
    }

    const renderFn =
      visualizerStyle === 'full-surface' ? renderFullSurface
        : renderContourBars

    function render() {
      const markEnd = perfMarkStart('visualizer:frame')
      renderFn()
      markEnd()
      timerRef.current = setTimeout(render, 33)
    }

    if (isPlaying && settings.canvasVisualizer) {
      timerRef.current = setTimeout(render, 33)
    } else {
      const { w, h } = sizeRef.current
      ctx.clearRect(0, 0, w, h)
    }

    return () => {
      observer.disconnect()
      if (resizeRafId !== null) cancelAnimationFrame(resizeRafId)
      if (timerRef.current) {
        clearTimeout(timerRef.current)
        timerRef.current = null
      }
    }
  // Intentionally omit intensity/colorMode/customColor/accentColor/segmentation —
  // these are read per-frame from refs to avoid teardown/rebuild on slider drags.
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [contourData, analyserRef, dataArrayRef, isPlaying, settings.canvasVisualizer, visualizerStyle])

  return (
    <canvas
      ref={canvasRef}
      className="absolute inset-0 w-full h-full pointer-events-none rounded-2xl"
      style={{ zIndex: 1 }}
    />
  )
}

const COLOR_PATTERN = /(\d+)\s*,\s*(\d+)\s*,\s*(\d+)/

function parseColor(colorStr: string | undefined): ParsedColor {
  if (!colorStr) return { r: 200, g: 200, b: 255 }
  // Handle hex colors (#rrggbb or #rgb)
  if (colorStr.startsWith('#')) return hexToRgb(colorStr)
  const match = colorStr.match(COLOR_PATTERN)
  if (match) {
    return { r: parseInt(match[1]!), g: parseInt(match[2]!), b: parseInt(match[3]!) }
  }
  return { r: 200, g: 200, b: 255 }
}

function hexToRgb(hex: string): ParsedColor {
  const h = hex.replace('#', '')
  if (h.length === 3) {
    return { r: parseInt(h[0]! + h[0]!, 16), g: parseInt(h[1]! + h[1]!, 16), b: parseInt(h[2]! + h[2]!, 16) }
  }
  return { r: parseInt(h.slice(0, 2), 16), g: parseInt(h.slice(2, 4), 16), b: parseInt(h.slice(4, 6), 16) }
}

const PRESET_COLORS: Record<string, ParsedColor> = {
  white:   { r: 255, g: 255, b: 255 },
  cyan:    { r: 0,   g: 230, b: 255 },
  magenta: { r: 255, g: 50,  b: 200 },
  gold:    { r: 255, g: 200, b: 50  },
  red:     { r: 255, g: 50,  b: 50  },
  green:   { r: 50,  g: 255, b: 100 },
}

function resolveBarColor(mode: string, customHex: string, accentColor: string): ParsedColor {
  if (mode === 'auto') return parseColor(accentColor)
  if (mode === 'custom') return parseColor(customHex)
  return PRESET_COLORS[mode] ?? parseColor(accentColor)
}
