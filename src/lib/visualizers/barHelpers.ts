/**
 * Fraction of FFT bins to use — drops the top 7% (high-frequency noise above ~20 kHz).
 */
const USABLE_FFT_FRACTION = 0.93

/** Power-law exponent for perceptual log-scale bar frequency distribution. */
const LOG_SCALE_EXP = 1.5

/** Temporal smoothing retention (higher = more inertia, slower decay). */
const SMOOTH_RETAIN = 0.4

/** Compute the usable FFT bin count from raw frequency data. */
export function usableBinCount(dataArray: Uint8Array): number {
  return Math.floor(dataArray.length * USABLE_FFT_FRACTION)
}

/**
 * Read an FFT value at a perceptual-log position, apply temporal smoothing,
 * and return the smoothed [0,1] amplitude.
 *
 * @param smoothed  - persistent smoothing buffer
 * @param index     - index into the smoothing buffer to read/write
 * @param t         - normalised bar position in [0,1)
 * @param binCount  - usable FFT bin count (from `usableBinCount`)
 * @param dataArray - raw frequency data (Uint8Array from AnalyserNode)
 */
export function sampleSmoothed(
  smoothed: Float32Array,
  index: number,
  t: number,
  binCount: number,
  dataArray: Uint8Array,
): number {
  const logIndex = Math.floor(Math.pow(t, LOG_SCALE_EXP) * (binCount - 1))
  const rawValue = dataArray[logIndex]! / 255
  smoothed[index] = smoothed[index]! * SMOOTH_RETAIN + rawValue * (1 - SMOOTH_RETAIN)
  return smoothed[index]!
}

export interface BarGeometry {
  px: number
  py: number
  ex: number
  ey: number
  alpha: number
}

export const ALPHA_BUCKETS = 11

export function createBarPool(size: number): BarGeometry[] {
  return Array.from({ length: size }, () => ({ px: 0, py: 0, ex: 0, ey: 0, alpha: 0 }))
}

export function createBucketIndices(): number[][] {
  return Array.from({ length: ALPHA_BUCKETS }, () => [])
}

export function clearBuckets(buckets: number[][]) {
  for (let b = 0; b < ALPHA_BUCKETS; b++) buckets[b]!.length = 0
}

/** 3-pass line-based rendering (shadow/glow/core) for line-geometry bars */
export function drawLineBars(
  ctx: CanvasRenderingContext2D,
  barPool: BarGeometry[],
  barCount: number,
  bucketIndices: number[][],
  shadowWidth: number,
  glowWidth: number,
  coreWidth: number,
  glowR: number, glowG: number, glowB: number,
  coreR: number, coreG: number, coreB: number,
  glowAlphaMul: number,
  coreAlphaMul: number
) {
  ctx.lineCap = 'round'

  // Shadow pass
  ctx.lineWidth = shadowWidth
  for (let b = 0; b < ALPHA_BUCKETS; b++) {
    const indices = bucketIndices[b]!
    if (indices.length === 0) continue
    ctx.strokeStyle = `rgba(0, 0, 0, ${(b / 10) * 0.35})`
    ctx.beginPath()
    for (let j = 0; j < indices.length; j++) {
      const bar = barPool[indices[j]!]!
      ctx.moveTo(bar.px, bar.py)
      ctx.lineTo(bar.ex, bar.ey)
    }
    ctx.stroke()
  }

  // Glow pass
  ctx.lineWidth = glowWidth
  for (let b = 0; b < ALPHA_BUCKETS; b++) {
    const indices = bucketIndices[b]!
    if (indices.length === 0) continue
    ctx.strokeStyle = `rgba(${glowR}, ${glowG}, ${glowB}, ${(b / 10) * glowAlphaMul})`
    ctx.beginPath()
    for (let j = 0; j < indices.length; j++) {
      const bar = barPool[indices[j]!]!
      ctx.moveTo(bar.px, bar.py)
      ctx.lineTo(bar.ex, bar.ey)
    }
    ctx.stroke()
  }

  // Core pass
  ctx.lineWidth = coreWidth
  for (let b = 0; b < ALPHA_BUCKETS; b++) {
    const indices = bucketIndices[b]!
    if (indices.length === 0) continue
    ctx.strokeStyle = `rgba(${coreR}, ${coreG}, ${coreB}, ${(b / 10) * coreAlphaMul})`
    ctx.beginPath()
    for (let j = 0; j < indices.length; j++) {
      const bar = barPool[indices[j]!]!
      ctx.moveTo(bar.px, bar.py)
      ctx.lineTo(bar.ex, bar.ey)
    }
    ctx.stroke()
  }
}
