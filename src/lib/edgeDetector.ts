/**
 * Multi-pass Canny-like edge detection on album artwork.
 * Runs on a 128x128 offscreen canvas.
 *
 * Three passes at different sensitivities so both bold and subtle
 * features are captured. The contour extractor picks the best result.
 */

import type { EdgeResult } from '../types'
import { luminance } from './imageUtils'

const DETECT_SIZE = 128
const MARGIN = 5

export function detectEdges(src: string | null | undefined): Promise<EdgeResult[] | null> {
  return new Promise((resolve) => {
    if (!src || src.startsWith('data:image/svg+xml')) {
      resolve(null)
      return
    }

    const img = new Image()
    img.crossOrigin = 'anonymous'
    img.onload = () => {
      try {
        const canvas = new OffscreenCanvas(DETECT_SIZE, DETECT_SIZE)
        const ctx = canvas.getContext('2d')
        if (!ctx) { resolve(null); return }
        ctx.drawImage(img, 0, 0, DETECT_SIZE, DETECT_SIZE)
        const data = ctx.getImageData(0, 0, DETECT_SIZE, DETECT_SIZE).data
        const w = DETECT_SIZE
        const h = DETECT_SIZE

        // Shared grayscale
        const gray = new Float32Array(w * h)
        for (let i = 0; i < w * h; i++) {
          const idx = i * 4
          gray[i] = luminance(data[idx]!, data[idx + 1]!, data[idx + 2]!)
        }

        // Pre-compute blur levels
        const blur1 = gaussianBlur5x5(gray, w, h)
        const blur2 = gaussianBlur5x5(blur1, w, h)

        // Pass 1: Heavy blur, high thresholds — only the boldest shapes
        const edges1 = runPipeline(blur2, w, h, 0.35, 0.15)

        // Pass 2: Medium blur, medium thresholds
        const edges2 = runPipeline(blur1, w, h, 0.25, 0.10)

        // Pass 3: Light blur, lower thresholds — catches subtle features
        const edges3 = runPipeline(blur1, w, h, 0.15, 0.06)

        resolve([
          { edges: edges1, width: w, height: h },
          { edges: edges2, width: w, height: h },
          { edges: edges3, width: w, height: h },
        ])
      } catch {
        resolve(null)
      }
    }
    img.onerror = () => resolve(null)
    img.src = src
  })
}

function runPipeline(blurred: Float32Array, w: number, h: number, highMul: number, lowMul: number): Uint8Array {
  const { magnitude, direction } = sobelGradient(blurred, w, h)
  const nms = nonMaxSuppression(magnitude, direction, w, h)
  const edges = hysteresis(nms, w, h, highMul, lowMul)
  maskBorder(edges, w, h, MARGIN)
  return edges
}

function gaussianBlur5x5(src: Float32Array, w: number, h: number): Float32Array {
  const kernel = [
    1, 4, 7, 4, 1,
    4, 16, 26, 16, 4,
    7, 26, 41, 26, 7,
    4, 16, 26, 16, 4,
    1, 4, 7, 4, 1,
  ]
  const kSum = 273
  const out = new Float32Array(w * h)

  for (let y = 2; y < h - 2; y++) {
    for (let x = 2; x < w - 2; x++) {
      let sum = 0
      let ki = 0
      for (let ky = -2; ky <= 2; ky++) {
        for (let kx = -2; kx <= 2; kx++) {
          sum += src[(y + ky) * w + (x + kx)]! * kernel[ki++]!
        }
      }
      out[y * w + x] = sum / kSum
    }
  }
  return out
}

function sobelGradient(src: Float32Array, w: number, h: number): { magnitude: Float32Array; direction: Float32Array } {
  const magnitude = new Float32Array(w * h)
  const direction = new Float32Array(w * h)

  for (let y = 1; y < h - 1; y++) {
    for (let x = 1; x < w - 1; x++) {
      const tl = src[(y - 1) * w + (x - 1)]!
      const t = src[(y - 1) * w + x]!
      const tr = src[(y - 1) * w + (x + 1)]!
      const l = src[y * w + (x - 1)]!
      const r = src[y * w + (x + 1)]!
      const bl = src[(y + 1) * w + (x - 1)]!
      const b = src[(y + 1) * w + x]!
      const br = src[(y + 1) * w + (x + 1)]!

      const gx = -tl + tr - 2 * l + 2 * r - bl + br
      const gy = -tl - 2 * t - tr + bl + 2 * b + br

      const idx = y * w + x
      magnitude[idx] = Math.sqrt(gx * gx + gy * gy)
      direction[idx] = Math.atan2(gy, gx)
    }
  }

  return { magnitude, direction }
}

function nonMaxSuppression(mag: Float32Array, dir: Float32Array, w: number, h: number): Float32Array {
  const out = new Float32Array(w * h)

  for (let y = 1; y < h - 1; y++) {
    for (let x = 1; x < w - 1; x++) {
      const idx = y * w + x
      const angle = ((dir[idx]! * 180) / Math.PI + 180) % 180
      let n1 = 0, n2 = 0

      if (angle < 22.5 || angle >= 157.5) {
        n1 = mag[y * w + (x - 1)]!
        n2 = mag[y * w + (x + 1)]!
      } else if (angle < 67.5) {
        n1 = mag[(y - 1) * w + (x + 1)]!
        n2 = mag[(y + 1) * w + (x - 1)]!
      } else if (angle < 112.5) {
        n1 = mag[(y - 1) * w + x]!
        n2 = mag[(y + 1) * w + x]!
      } else {
        n1 = mag[(y - 1) * w + (x - 1)]!
        n2 = mag[(y + 1) * w + (x + 1)]!
      }

      out[idx] = mag[idx]! >= n1 && mag[idx]! >= n2 ? mag[idx]! : 0
    }
  }

  return out
}

function hysteresis(nms: Float32Array, w: number, h: number, highMul: number, lowMul: number): Uint8Array {
  let maxMag = 0
  for (let i = 0; i < nms.length; i++) {
    if (nms[i]! > maxMag) maxMag = nms[i]!
  }

  if (maxMag === 0) return new Uint8Array(w * h)

  const highThresh = maxMag * highMul
  const lowThresh = maxMag * lowMul
  const edges = new Uint8Array(w * h)

  const stack: number[] = []
  for (let i = 0; i < nms.length; i++) {
    if (nms[i]! >= highThresh) {
      edges[i] = 255
      stack.push(i)
    }
  }

  while (stack.length > 0) {
    const idx = stack.pop()!
    const x = idx % w
    const y = (idx - x) / w

    for (let dy = -1; dy <= 1; dy++) {
      for (let dx = -1; dx <= 1; dx++) {
        if (dx === 0 && dy === 0) continue
        const nx = x + dx
        const ny = y + dy
        if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue
        const ni = ny * w + nx
        if (edges[ni] === 0 && nms[ni]! >= lowThresh) {
          edges[ni] = 255
          stack.push(ni)
        }
      }
    }
  }

  return edges
}

function maskBorder(edges: Uint8Array, w: number, h: number, margin: number) {
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      if (x < margin || x >= w - margin || y < margin || y >= h - margin) {
        edges[y * w + x] = 0
      }
    }
  }
}
