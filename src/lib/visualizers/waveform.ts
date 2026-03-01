import type { VisualizerRenderer, RenderContext } from './types'

const POINT_COUNT = 128

export function createWaveformRenderer(): VisualizerRenderer {
  const smoothedPoints = new Float32Array(POINT_COUNT)

  return {
    name: 'waveform',
    needsTimeDomain: true,
    needsContourData: false,
    smoothedSize: 0, // waveform manages its own smoothing

    init() {
      smoothedPoints.fill(0.5) // center line = 128/255 ~ 0.5
    },

    render(rc: RenderContext) {
      const { ctx, w, h, timeDomainArray, style } = rc
      ctx.clearRect(0, 0, w, h)
      if (w === 0 || h === 0 || !timeDomainArray) return

      const { glowR, glowG, glowB, coreR, coreG, coreB, glowAlphaMul, coreAlphaMul } = style

      // Subsample time-domain data to POINT_COUNT points
      const step = timeDomainArray.length / POINT_COUNT
      for (let i = 0; i < POINT_COUNT; i++) {
        const rawIndex = Math.floor(i * step)
        const raw = timeDomainArray[rawIndex]! / 255 // 0..1, centered at 0.5
        // Lighter smoothing (0.3 retention) for responsiveness
        smoothedPoints[i] = smoothedPoints[i]! * 0.3 + raw * 0.7
      }

      const marginX = w * 0.05
      const drawW = w - marginX * 2
      const centerY = h / 2
      const amplitude = h * 0.4

      // Build path using quadratic curves through midpoints
      function buildPath() {
        ctx.beginPath()
        const x0 = marginX
        const y0 = centerY + (smoothedPoints[0]! - 0.5) * 2 * amplitude
        ctx.moveTo(x0, y0)

        for (let i = 0; i < POINT_COUNT - 1; i++) {
          const xCurr = marginX + (i / (POINT_COUNT - 1)) * drawW
          const yCurr = centerY + (smoothedPoints[i]! - 0.5) * 2 * amplitude
          const xNext = marginX + ((i + 1) / (POINT_COUNT - 1)) * drawW
          const yNext = centerY + (smoothedPoints[i + 1]! - 0.5) * 2 * amplitude
          const cpX = (xCurr + xNext) / 2
          const cpY = (yCurr + yNext) / 2
          ctx.quadraticCurveTo(xCurr, yCurr, cpX, cpY)
        }

        // Final point
        const xLast = marginX + drawW
        const yLast = centerY + (smoothedPoints[POINT_COUNT - 1]! - 0.5) * 2 * amplitude
        ctx.lineTo(xLast, yLast)
      }

      ctx.lineCap = 'round'
      ctx.lineJoin = 'round'

      // Shadow pass
      ctx.lineWidth = 6
      ctx.strokeStyle = `rgba(0, 0, 0, ${0.35 * glowAlphaMul})`
      buildPath()
      ctx.stroke()

      // Glow pass
      ctx.lineWidth = 3
      ctx.strokeStyle = `rgba(${glowR}, ${glowG}, ${glowB}, ${glowAlphaMul})`
      buildPath()
      ctx.stroke()

      // Core pass
      ctx.lineWidth = 1.5
      ctx.strokeStyle = `rgba(${coreR}, ${coreG}, ${coreB}, ${coreAlphaMul})`
      buildPath()
      ctx.stroke()
    },

    dispose() {}
  }
}
