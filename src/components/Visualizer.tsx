import { useRef, useEffect } from 'react'
import type React from 'react'
import { getFrequencyData, getTimeDomainData } from '../lib/audioAnalyser'
import { perfMarkStart } from '../lib/perf'
import useVisualSettings from '../hooks/useVisualSettings'
import { createRenderer, computeFrameStyle } from '../lib/visualizers'
import type { VisualizerRenderer } from '../lib/visualizers'
import type { ContourData, VisualizerStyle, SegmentationResult } from '../types'

interface VisualizerProps {
  contourData: ContourData | null
  analyserRef: React.RefObject<AnalyserNode | null>
  dataArrayRef: React.RefObject<Uint8Array | null>
  accentColor: string
  isPlaying: boolean
  segmentation?: SegmentationResult | null
  /** Pre-resolved concrete style (random already picked by parent). */
  resolvedStyle: Exclude<VisualizerStyle, 'random'>
}

/**
 * Canvas overlay that draws audio-reactive visualizations.
 * Delegates rendering to style-specific renderer modules.
 */
export default function Visualizer({ contourData, analyserRef, dataArrayRef, accentColor, isPlaying, segmentation, resolvedStyle }: VisualizerProps) {
  const { settings } = useVisualSettings()
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const rafRef = useRef<number | null>(null)
  const smoothedRef = useRef<Float32Array | null>(null)
  const sizeRef = useRef<{ w: number; h: number }>({ w: 0, h: 0 })
  const timeDomainRef = useRef<Uint8Array | null>(null)

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

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return

    const ctx = canvas.getContext('2d')
    if (!ctx) return

    let resizeRafId: number | null = null

    // Create the renderer for the resolved style (random already picked by parent)
    const renderer: VisualizerRenderer = createRenderer(resolvedStyle)

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
      // Re-init renderer on resize
      renderer.init(w, h, contourData)
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

    // Initialize renderer
    const { w, h } = sizeRef.current
    renderer.init(w, h, contourData)

    // Allocate smoothed array based on renderer's needs
    if (!smoothedRef.current || smoothedRef.current.length !== renderer.smoothedSize) {
      smoothedRef.current = new Float32Array(renderer.smoothedSize)
    }

    // Allocate time-domain buffer if renderer needs it
    if (renderer.needsTimeDomain) {
      const analyser = analyserRef?.current
      const fftSize = analyser?.fftSize ?? 512
      if (!timeDomainRef.current || timeDomainRef.current.length !== fftSize) {
        timeDomainRef.current = new Uint8Array(fftSize)
      }
    }

    function render() {
      const markEnd = perfMarkStart('visualizer:frame')

      const analyser = analyserRef?.current
      const dataArray = dataArrayRef?.current

      if (analyser && dataArray) {
        getFrequencyData(analyser, dataArray as Uint8Array<ArrayBuffer>)

        if (renderer.needsTimeDomain && timeDomainRef.current) {
          getTimeDomainData(analyser, timeDomainRef.current as Uint8Array<ArrayBuffer>)
        }

        // Ensure smoothed array matches renderer expectations
        if (!smoothedRef.current || smoothedRef.current.length !== renderer.smoothedSize) {
          smoothedRef.current = new Float32Array(renderer.smoothedSize)
        }

        const style = computeFrameStyle(
          intensityRef.current,
          colorModeRef.current,
          customColorRef.current,
          accentColorRef.current,
          !!segmentationRef.current
        )

        renderer.render(
          {
            ctx: ctx!,
            w: sizeRef.current.w,
            h: sizeRef.current.h,
            dataArray,
            timeDomainArray: renderer.needsTimeDomain ? timeDomainRef.current : null,
            style,
            intensity: intensityRef.current / 100,
          },
          smoothedRef.current
        )
      }

      markEnd()
      rafRef.current = requestAnimationFrame(render)
    }

    if (isPlaying && settings.canvasVisualizer) {
      rafRef.current = requestAnimationFrame(render)
    } else {
      const { w, h } = sizeRef.current
      ctx.clearRect(0, 0, w, h)
    }

    return () => {
      observer.disconnect()
      if (resizeRafId !== null) cancelAnimationFrame(resizeRafId)
      if (rafRef.current !== null) {
        cancelAnimationFrame(rafRef.current)
        rafRef.current = null
      }
      renderer.dispose()
    }
  // Intentionally omit intensity/colorMode/customColor/accentColor/segmentation —
  // these are read per-frame from refs to avoid teardown/rebuild on slider drags.
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [contourData, analyserRef, dataArrayRef, isPlaying, settings.canvasVisualizer, resolvedStyle])

  return (
    <canvas
      ref={canvasRef}
      className="absolute inset-0 w-full h-full pointer-events-none rounded-2xl"
      style={{ zIndex: 1 }}
    />
  )
}
