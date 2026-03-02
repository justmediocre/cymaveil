import type { VisualizerRenderer, RenderContext } from './types'
import { usableBinCount, sampleSmoothed } from './barHelpers'

const BAR_COUNT = 48

export function createMirroredBarsRenderer(): VisualizerRenderer {
  return {
    name: 'mirrored-bars',
    needsTimeDomain: false,
    needsContourData: false,
    smoothedSize: BAR_COUNT,

    init() {},

    render(rc: RenderContext, smoothed: Float32Array) {
      const { ctx, w, h, dataArray, style } = rc
      ctx.clearRect(0, 0, w, h)
      if (w === 0 || h === 0) return

      const { glowR, glowG, glowB, coreR, coreG, coreB, glowAlphaMul, coreAlphaMul, padX, padBot } = style
      const binCount = usableBinCount(dataArray)

      const insetL = w * padX
      const insetR = w * padX
      const areaW = w - insetL - insetR
      const centerY = h / 2

      const barWidth = areaW / BAR_COUNT
      const gap = barWidth * 0.15
      const actualBarWidth = barWidth - gap
      const maxHalfHeight = (h / 2) * 0.85

      // Smooth values
      for (let i = 0; i < BAR_COUNT; i++) {
        sampleSmoothed(smoothed, i, (i + 1) / (BAR_COUNT + 1), binCount, dataArray)
      }

      // Shadow pass
      for (let i = 0; i < BAR_COUNT; i++) {
        const value = smoothed[i]!
        if (value < 0.02) continue

        const barHalfH = value * maxHalfHeight
        const x = insetL + i * barWidth + gap / 2
        const alpha = 0.4 + value * 0.6

        ctx.fillStyle = `rgba(0, 0, 0, ${alpha * 0.35})`
        ctx.fillRect(x - 3, centerY - barHalfH - 3, actualBarWidth + 6, barHalfH * 2 + 6)
      }

      // Glow pass
      for (let i = 0; i < BAR_COUNT; i++) {
        const value = smoothed[i]!
        if (value < 0.02) continue

        const barHalfH = value * maxHalfHeight
        const x = insetL + i * barWidth + gap / 2
        const alpha = 0.4 + value * 0.6

        // Top half (grows upward from center)
        const gradUp = ctx.createLinearGradient(0, centerY, 0, centerY - barHalfH)
        gradUp.addColorStop(0, `rgba(${glowR}, ${glowG}, ${glowB}, 0)`)
        gradUp.addColorStop(1, `rgba(${glowR}, ${glowG}, ${glowB}, ${alpha * glowAlphaMul})`)
        ctx.fillStyle = gradUp
        ctx.fillRect(x - 2, centerY - barHalfH - 2, actualBarWidth + 4, barHalfH + 2)

        // Bottom half (grows downward from center)
        const gradDown = ctx.createLinearGradient(0, centerY, 0, centerY + barHalfH)
        gradDown.addColorStop(0, `rgba(${glowR}, ${glowG}, ${glowB}, 0)`)
        gradDown.addColorStop(1, `rgba(${glowR}, ${glowG}, ${glowB}, ${alpha * glowAlphaMul})`)
        ctx.fillStyle = gradDown
        ctx.fillRect(x - 2, centerY, actualBarWidth + 4, barHalfH + 2)
      }

      // Core pass
      for (let i = 0; i < BAR_COUNT; i++) {
        const value = smoothed[i]!
        if (value < 0.02) continue

        const barHalfH = value * maxHalfHeight
        const x = insetL + i * barWidth + gap / 2
        const alpha = 0.4 + value * 0.6

        // Top half
        const gradUp = ctx.createLinearGradient(0, centerY, 0, centerY - barHalfH)
        gradUp.addColorStop(0, `rgba(${glowR}, ${glowG}, ${glowB}, 0)`)
        gradUp.addColorStop(0.3, `rgba(${glowR}, ${glowG}, ${glowB}, ${alpha * coreAlphaMul * 0.8})`)
        gradUp.addColorStop(1, `rgba(${coreR}, ${coreG}, ${coreB}, ${alpha * coreAlphaMul})`)
        ctx.fillStyle = gradUp
        ctx.fillRect(x, centerY - barHalfH, actualBarWidth, barHalfH)

        // Bottom half
        const gradDown = ctx.createLinearGradient(0, centerY, 0, centerY + barHalfH)
        gradDown.addColorStop(0, `rgba(${glowR}, ${glowG}, ${glowB}, 0)`)
        gradDown.addColorStop(0.3, `rgba(${glowR}, ${glowG}, ${glowB}, ${alpha * coreAlphaMul * 0.8})`)
        gradDown.addColorStop(1, `rgba(${coreR}, ${coreG}, ${coreB}, ${alpha * coreAlphaMul})`)
        ctx.fillStyle = gradDown
        ctx.fillRect(x, centerY, actualBarWidth, barHalfH)
      }
    },

    dispose() {}
  }
}
