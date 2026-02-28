/**
 * Converts binary edge maps into a single ordered, drawable contour path.
 *
 * Accepts multiple edge maps (from multi-pass detection) and pools
 * all connected components. Picks the best mostly-horizontal contour
 * (>= 60% of image width) by horizontal span x center-proximity score.
 * Falls back to a gentle parabola when no wide contour is found.
 * Bars always point straight up (nx=0, ny=-1).
 */

import type { EdgeResult, ContourData, ContourPoint, Contour } from '../types'

interface Point {
  x: number
  y: number
}

interface Component {
  pixels: Point[]
  minX: number
  maxX: number
  minY: number
  maxY: number
  span: number
  score: number
}

const MIN_POINTS = 24
const MAX_POINTS = 80
const POINTS_PER_UNIT_LENGTH = 200
const MIN_HSPAN = 0.6         // minimum horizontal span as fraction of image width
const SMOOTH_WINDOW = 9       // path smoothing (wider = fewer kinks)

export function extractContour(edgeResults: EdgeResult[] | null): ContourData {
  if (!edgeResults || edgeResults.length === 0) {
    return { contours: [fallbackContour()], isFallback: true }
  }

  const { width, height } = edgeResults[0]!
  const imgDiag = Math.sqrt(width * width + height * height)

  // 1. Collect components from ALL edge maps
  const allComponents: Component[] = []
  for (const edgeResult of edgeResults) {
    const components = labelComponents(edgeResult.edges, edgeResult.width, edgeResult.height)
    allComponents.push(...components)
  }

  if (allComponents.length === 0) {
    return { contours: [fallbackContour()], isFallback: true }
  }

  // 2. Score each component: horizontal span x center proximity
  const cx = width / 2
  const cy = height / 2
  const maxCenterDist = imgDiag / 2

  for (const comp of allComponents) {
    comp.span = (comp.maxX - comp.minX) / width

    const comX = (comp.minX + comp.maxX) / 2
    const comY = (comp.minY + comp.maxY) / 2
    const distFromCenter = Math.sqrt((comX - cx) ** 2 + (comY - cy) ** 2)
    const centerWeight = 1.0 - 0.5 * (distFromCenter / maxCenterDist)
    comp.score = comp.span * centerWeight
  }

  // 3. Single pass — pick the best-scoring component with sufficient horizontal extent
  let best: Component | undefined
  for (const c of allComponents) {
    if (c.span >= MIN_HSPAN && c.pixels.length >= 10 && (!best || c.score > best.score)) {
      best = c
    }
  }
  if (!best) {
    return { contours: [fallbackContour()], isFallback: true }
  }

  // 4. Process into a drawable contour
  const ordered = orderPixels(best.pixels)
  const smoothed = smoothPath(ordered, SMOOTH_WINDOW)

  const arcLen = computeArcLength(smoothed)
  const normalizedArcLen = arcLen / imgDiag
  const numPoints = Math.max(MIN_POINTS, Math.min(MAX_POINTS,
    Math.round(normalizedArcLen * POINTS_PER_UNIT_LENGTH)
  ))

  const resampled = resamplePath(smoothed, numPoints)

  // Normalize positions to [0,1] and force all bars vertical (straight up)
  const points: ContourPoint[] = resampled.map((p) => ({
    x: p.x / width,
    y: p.y / height,
    nx: 0,
    ny: -1,
  }))

  return { contours: [{ points, span: best.span }], isFallback: false }
}

function labelComponents(edges: Uint8Array, w: number, h: number): Component[] {
  const visited = new Uint8Array(w * h)
  const components: Component[] = []

  for (let i = 0; i < edges.length; i++) {
    if (edges[i] === 0 || visited[i]) continue

    const pixels: Point[] = []
    const stack = [i]
    visited[i] = 1
    let minX = w, maxX = 0, minY = h, maxY = 0

    while (stack.length > 0) {
      const idx = stack.pop()!
      const x = idx % w
      const y = (idx - x) / w
      pixels.push({ x, y })

      if (x < minX) minX = x
      if (x > maxX) maxX = x
      if (y < minY) minY = y
      if (y > maxY) maxY = y

      for (let dy = -1; dy <= 1; dy++) {
        for (let dx = -1; dx <= 1; dx++) {
          if (dx === 0 && dy === 0) continue
          const nx = x + dx
          const ny = y + dy
          if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue
          const ni = ny * w + nx
          if (edges[ni] !== 0 && !visited[ni]) {
            visited[ni] = 1
            stack.push(ni)
          }
        }
      }
    }

    components.push({ pixels, minX, maxX, minY, maxY, span: 0, score: 0 })
  }

  return components
}

function orderPixels(pixels: Point[]): Point[] {
  if (pixels.length <= 1) return pixels

  const gridSize = 4
  const grid = new Map<string, number[]>()
  for (let i = 0; i < pixels.length; i++) {
    const key = `${Math.floor(pixels[i]!.x / gridSize)},${Math.floor(pixels[i]!.y / gridSize)}`
    if (!grid.has(key)) grid.set(key, [])
    grid.get(key)!.push(i)
  }

  const used = new Uint8Array(pixels.length)
  const ordered: Point[] = []

  let startIdx = 0
  for (let i = 1; i < pixels.length; i++) {
    if (pixels[i]!.y < pixels[startIdx]!.y ||
      (pixels[i]!.y === pixels[startIdx]!.y && pixels[i]!.x < pixels[startIdx]!.x)) {
      startIdx = i
    }
  }

  used[startIdx] = 1
  ordered.push(pixels[startIdx]!)

  while (ordered.length < pixels.length) {
    const current = ordered[ordered.length - 1]!
    const cxg = Math.floor(current.x / gridSize)
    const cyg = Math.floor(current.y / gridSize)

    let bestIdx = -1
    let bestDist = Infinity

    for (let gy = cyg - 2; gy <= cyg + 2; gy++) {
      for (let gx = cxg - 2; gx <= cxg + 2; gx++) {
        const cell = grid.get(`${gx},${gy}`)
        if (!cell) continue
        for (const idx of cell) {
          if (used[idx]) continue
          const p = pixels[idx]!
          const dx = p.x - current.x
          const dy = p.y - current.y
          const dist = dx * dx + dy * dy
          if (dist < bestDist) {
            bestDist = dist
            bestIdx = idx
          }
        }
      }
    }

    if (bestIdx === -1 || bestDist > 36) {
      bestDist = Infinity
      for (let i = 0; i < pixels.length; i++) {
        if (used[i]) continue
        const p = pixels[i]!
        const dx = p.x - current.x
        const dy = p.y - current.y
        const dist = dx * dx + dy * dy
        if (dist < bestDist) {
          bestDist = dist
          bestIdx = i
        }
      }
    }

    if (bestIdx === -1 || bestDist > 64) break

    used[bestIdx] = 1
    ordered.push(pixels[bestIdx]!)
  }

  return ordered
}

function computeArcLength(points: Point[]): number {
  let len = 0
  for (let i = 1; i < points.length; i++) {
    const dx = points[i]!.x - points[i - 1]!.x
    const dy = points[i]!.y - points[i - 1]!.y
    len += Math.sqrt(dx * dx + dy * dy)
  }
  return len
}

function smoothPath(points: Point[], windowSize: number): Point[] {
  const half = Math.floor(windowSize / 2)
  return points.map((_, i) => {
    let sx = 0, sy = 0, count = 0
    for (let j = -half; j <= half; j++) {
      const idx = Math.max(0, Math.min(points.length - 1, i + j))
      sx += points[idx]!.x
      sy += points[idx]!.y
      count++
    }
    return { x: sx / count, y: sy / count }
  })
}

function resamplePath(points: Point[], n: number): Point[] {
  if (points.length < 2) return points

  const arcLengths = [0]
  for (let i = 1; i < points.length; i++) {
    const dx = points[i]!.x - points[i - 1]!.x
    const dy = points[i]!.y - points[i - 1]!.y
    arcLengths.push(arcLengths[i - 1]! + Math.sqrt(dx * dx + dy * dy))
  }

  const totalLength = arcLengths[arcLengths.length - 1]!
  if (totalLength === 0) return points.slice(0, n)

  const resampled: Point[] = []
  let srcIdx = 0

  for (let i = 0; i < n; i++) {
    const targetDist = (i / (n - 1)) * totalLength

    while (srcIdx < arcLengths.length - 1 && arcLengths[srcIdx + 1]! < targetDist) {
      srcIdx++
    }

    if (srcIdx >= points.length - 1) {
      resampled.push({ ...points[points.length - 1]! })
      continue
    }

    const segLen = arcLengths[srcIdx + 1]! - arcLengths[srcIdx]!
    const t = segLen > 0 ? (targetDist - arcLengths[srcIdx]!) / segLen : 0

    resampled.push({
      x: points[srcIdx]!.x + t * (points[srcIdx + 1]!.x - points[srcIdx]!.x),
      y: points[srcIdx]!.y + t * (points[srcIdx + 1]!.y - points[srcIdx]!.y),
    })
  }

  return resampled
}

function fallbackContour(): Contour {
  const points: ContourPoint[] = []
  const n = 48

  for (let i = 0; i < n; i++) {
    const t = i / (n - 1)
    points.push({
      x: t,
      y: 0.92 - 0.32 * t * (1 - t),
      nx: 0,
      ny: -1,
    })
  }

  return { points, span: 1.0 }
}
