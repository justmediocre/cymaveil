import type { ContourData } from '../../types'
import type { VisualizerRenderer, RenderContext } from './types'
import { type BarGeometry, createBarPool, createBucketIndices, clearBuckets, drawLineBars, ALPHA_BUCKETS } from './barHelpers'

export function createContourBarsRenderer(): VisualizerRenderer {
  let contours: ContourData['contours'] = []
  let barPool: BarGeometry[] = []
  let bucketIndices: number[][] = createBucketIndices()

  return {
    name: 'contour-bars',
    needsTimeDomain: false,
    needsContourData: true,
    smoothedSize: 0,

    init(_w: number, _h: number, contourData?: ContourData | null) {
      contours = contourData?.contours || []
      const totalPoints = contours.reduce((sum, c) => sum + c.points.length, 0)
      this.smoothedSize = totalPoints
      barPool = createBarPool(Math.max(totalPoints, 48))
      bucketIndices = createBucketIndices()
    },

    render(rc: RenderContext, smoothed: Float32Array) {
      const { ctx, w, h, dataArray, style } = rc
      ctx.clearRect(0, 0, w, h)

      if (contours.length === 0 || w === 0) return

      const { glowR, glowG, glowB, coreR, coreG, coreB, glowAlphaMul, coreAlphaMul, padX, padBot } = style
      const binCount = Math.floor(dataArray.length * 0.93)
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

        let barCount = 0
        clearBuckets(bucketIndices)

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

        drawLineBars(ctx, barPool, barCount, bucketIndices,
          shadowWidth, glowWidth, coreWidth,
          glowR, glowG, glowB, coreR, coreG, coreB,
          glowAlphaMul, coreAlphaMul)

        smoothedOffset += points.length
      }
    },

    dispose() {
      contours = []
      barPool = []
    }
  }
}
