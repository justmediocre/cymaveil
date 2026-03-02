import type { VisualizerRenderer, RenderContext } from './types'
import { usableBinCount, sampleSmoothed, LINE_WIDTH_SHADOW, LINE_WIDTH_GLOW, LINE_WIDTH_CORE } from './barHelpers'

const BAR_COUNT = 64

export function createRadialBurstRenderer(): VisualizerRenderer {
  return {
    name: 'radial-burst',
    needsTimeDomain: false,
    needsContourData: false,
    smoothedSize: BAR_COUNT,

    init() {},

    render(rc: RenderContext, hostSmoothed: Float32Array) {
      const { ctx, w, h, dataArray, style } = rc
      ctx.clearRect(0, 0, w, h)
      if (w === 0 || h === 0) return

      const { glowR, glowG, glowB, coreR, coreG, coreB, glowAlphaMul, coreAlphaMul } = style
      const binCount = usableBinCount(dataArray)

      const cx = w / 2
      const cy = h / 2
      const maxRadius = Math.min(w, h) * 0.45
      const innerRadius = maxRadius * 0.15

      ctx.lineCap = 'round'

      // Build bar data
      for (let i = 0; i < BAR_COUNT; i++) {
        sampleSmoothed(hostSmoothed, i, (i + 1) / (BAR_COUNT + 1), binCount, dataArray)
      }

      const angleStep = (Math.PI * 2) / BAR_COUNT

      // Shadow pass
      ctx.lineWidth = LINE_WIDTH_SHADOW
      for (let i = 0; i < BAR_COUNT; i++) {
        const value = hostSmoothed[i]!
        if (value < 0.02) continue

        const angle = i * angleStep - Math.PI / 2
        const barLen = innerRadius + value * (maxRadius - innerRadius)
        const cos = Math.cos(angle)
        const sin = Math.sin(angle)
        const alpha = 0.5 + value * 0.5

        ctx.strokeStyle = `rgba(0, 0, 0, ${alpha * 0.35})`
        ctx.beginPath()
        ctx.moveTo(cx + cos * innerRadius, cy + sin * innerRadius)
        ctx.lineTo(cx + cos * barLen, cy + sin * barLen)
        ctx.stroke()
      }

      // Glow pass
      ctx.lineWidth = LINE_WIDTH_GLOW
      for (let i = 0; i < BAR_COUNT; i++) {
        const value = hostSmoothed[i]!
        if (value < 0.02) continue

        const angle = i * angleStep - Math.PI / 2
        const barLen = innerRadius + value * (maxRadius - innerRadius)
        const cos = Math.cos(angle)
        const sin = Math.sin(angle)
        const alpha = 0.5 + value * 0.5

        ctx.strokeStyle = `rgba(${glowR}, ${glowG}, ${glowB}, ${alpha * glowAlphaMul})`
        ctx.beginPath()
        ctx.moveTo(cx + cos * innerRadius, cy + sin * innerRadius)
        ctx.lineTo(cx + cos * barLen, cy + sin * barLen)
        ctx.stroke()
      }

      // Core pass
      ctx.lineWidth = LINE_WIDTH_CORE
      for (let i = 0; i < BAR_COUNT; i++) {
        const value = hostSmoothed[i]!
        if (value < 0.02) continue

        const angle = i * angleStep - Math.PI / 2
        const barLen = innerRadius + value * (maxRadius - innerRadius)
        const cos = Math.cos(angle)
        const sin = Math.sin(angle)
        const alpha = 0.5 + value * 0.5

        ctx.strokeStyle = `rgba(${coreR}, ${coreG}, ${coreB}, ${alpha * coreAlphaMul})`
        ctx.beginPath()
        ctx.moveTo(cx + cos * innerRadius, cy + sin * innerRadius)
        ctx.lineTo(cx + cos * barLen, cy + sin * barLen)
        ctx.stroke()
      }
    },

    dispose() {}
  }
}
