import type { VisualizerRenderer, RenderContext } from './types'
import { type BarGeometry, createBarPool, usableBinCount, sampleSmoothed } from './barHelpers'

const BAR_COUNT = 48

export function createFullSurfaceRenderer(): VisualizerRenderer {
  let barPool: BarGeometry[] = createBarPool(BAR_COUNT)

  return {
    name: 'full-surface',
    needsTimeDomain: false,
    needsContourData: false,
    smoothedSize: BAR_COUNT,

    init() {
      barPool = createBarPool(BAR_COUNT)
    },

    render(rc: RenderContext, smoothed: Float32Array) {
      const { ctx, w, h, dataArray, style } = rc
      ctx.clearRect(0, 0, w, h)

      if (w === 0) return

      const { glowR, glowG, glowB, coreR, coreG, coreB, glowAlphaMul, coreAlphaMul, padX, padBot } = style
      const binCount = usableBinCount(dataArray)

      const insetL = w * padX
      const insetR = w * padX
      const insetB = h * padBot
      const areaW = w - insetL - insetR
      const areaH = h - insetB

      const barWidth = areaW / BAR_COUNT
      const gap = barWidth * 0.15
      const actualBarWidth = barWidth - gap

      let barCount = 0

      for (let i = 0; i < BAR_COUNT; i++) {
        const value = sampleSmoothed(smoothed, i, i / BAR_COUNT, binCount, dataArray)

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
        const gradient = ctx.createLinearGradient(0, bar.py, 0, bar.ey)
        gradient.addColorStop(0, `rgba(0, 0, 0, ${bar.alpha * 0.35})`)
        gradient.addColorStop(1, 'rgba(0, 0, 0, 0)')
        ctx.fillStyle = gradient
        ctx.fillRect(bar.px - 3, bar.py - 3, bar.ex - bar.px + 6, bar.ey - bar.py + 6)
      }

      // Glow pass
      for (let i = 0; i < barCount; i++) {
        const bar = barPool[i]!
        const gradient = ctx.createLinearGradient(0, bar.py, 0, bar.ey)
        gradient.addColorStop(0, `rgba(${glowR}, ${glowG}, ${glowB}, ${bar.alpha * glowAlphaMul})`)
        gradient.addColorStop(1, `rgba(${glowR}, ${glowG}, ${glowB}, 0)`)
        ctx.fillStyle = gradient
        ctx.fillRect(bar.px - 2, bar.py - 2, bar.ex - bar.px + 4, bar.ey - bar.py + 4)
      }

      // Core pass
      for (let i = 0; i < barCount; i++) {
        const bar = barPool[i]!
        const gradient = ctx.createLinearGradient(0, bar.py, 0, bar.ey)
        gradient.addColorStop(0, `rgba(${coreR}, ${coreG}, ${coreB}, ${bar.alpha * coreAlphaMul})`)
        gradient.addColorStop(0.7, `rgba(${glowR}, ${glowG}, ${glowB}, ${bar.alpha * coreAlphaMul * 0.8})`)
        gradient.addColorStop(1, `rgba(${glowR}, ${glowG}, ${glowB}, 0)`)
        ctx.fillStyle = gradient
        ctx.fillRect(bar.px, bar.py, bar.ex - bar.px, bar.ey - bar.py)
      }
    },

    dispose() {
      barPool = []
    }
  }
}
