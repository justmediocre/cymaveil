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
