/**
 * Extract dominant and accent colors from an image data URI using offscreen canvas pixel sampling.
 */

import type { AlbumColors } from '../types'

interface ColorBucket {
  r: number
  g: number
  b: number
  count: number
  saturation: number
}

/** Boost washed-out pastel accents into a vivid version of the same hue */
function vibrantAccent(r: number, g: number, b: number): string {
  const rn = r / 255, gn = g / 255, bn = b / 255
  const cmax = Math.max(rn, gn, bn), cmin = Math.min(rn, gn, bn)
  const delta = cmax - cmin

  // RGB → HSL
  let h = 0
  if (delta > 0) {
    if (cmax === rn) h = (((gn - bn) / delta) % 6 + 6) % 6
    else if (cmax === gn) h = (bn - rn) / delta + 2
    else h = (rn - gn) / delta + 4
    h *= 60
  }
  let l = (cmax + cmin) / 2
  let s = delta === 0 ? 0 : delta / (1 - Math.abs(2 * l - 1))

  // Only boost if there's actual hue to amplify (skip achromatic grays)
  if (s > 0.05) {
    s = Math.max(s, 0.5)
    l = Math.min(l, 0.65)
  }

  // HSL → RGB
  const c = (1 - Math.abs(2 * l - 1)) * s
  const x = c * (1 - Math.abs(((h / 60) % 2) - 1))
  const m = l - c / 2
  let ro: number, go: number, bo: number
  if (h < 60) { ro = c; go = x; bo = 0 }
  else if (h < 120) { ro = x; go = c; bo = 0 }
  else if (h < 180) { ro = 0; go = c; bo = x }
  else if (h < 240) { ro = 0; go = x; bo = c }
  else if (h < 300) { ro = x; go = 0; bo = c }
  else { ro = c; go = 0; bo = x }

  return `rgb(${Math.round((ro! + m) * 255)}, ${Math.round((go! + m) * 255)}, ${Math.round((bo! + m) * 255)})`
}

export function extractColors(src: string | null | undefined): Promise<AlbumColors | null> {
  return new Promise((resolve) => {
    if (!src || src.startsWith('data:image/svg+xml')) {
      resolve(null)
      return
    }

    const img = new Image()
    img.crossOrigin = 'anonymous'
    img.onload = () => {
      try {
        const size = 64 // small sample for speed
        const canvas = new OffscreenCanvas(size, size)
        const ctx = canvas.getContext('2d')
        if (!ctx) { resolve(null); return }
        ctx.drawImage(img, 0, 0, size, size)
        const data = ctx.getImageData(0, 0, size, size).data

        // Collect color buckets
        const buckets = new Map<string, ColorBucket>()

        for (let i = 0; i < data.length; i += 16) { // sample every 4th pixel
          const r = data[i]!
          const g = data[i + 1]!
          const b = data[i + 2]!

          // Skip very dark and very light pixels
          const brightness = (r + g + b) / 3
          if (brightness < 20 || brightness > 235) continue

          // Quantize to reduce color space
          const qr = Math.round(r / 32) * 32
          const qg = Math.round(g / 32) * 32
          const qb = Math.round(b / 32) * 32
          const key = `${qr},${qg},${qb}`

          const saturation = Math.max(r, g, b) - Math.min(r, g, b)

          if (!buckets.has(key)) {
            buckets.set(key, { r: qr, g: qg, b: qb, count: 0, saturation })
          }
          buckets.get(key)!.count++
        }

        if (buckets.size === 0) {
          resolve(null)
          return
        }

        // Sort by frequency
        const sorted = [...buckets.values()].sort((a, b) => b.count - a.count)

        // Dominant = most frequent color (darkened for background use)
        const dom = sorted[0]!
        const dominant = `rgb(${Math.round(dom.r * 0.5)}, ${Math.round(dom.g * 0.5)}, ${Math.round(dom.b * 0.5)})`

        // Accent = most vivid color, penalising near-white and near-black
        const accentScore = (c: ColorBucket) => {
          const lum = (c.r + c.g + c.b) / 3
          const lightPenalty = Math.max(0, (lum - 180) / 75)   // ramps 0→1 above lum 180
          const darkPenalty  = Math.max(0, (40 - lum) / 40)    // ramps 0→1 below lum 40
          return c.saturation * (1 - Math.max(lightPenalty, darkPenalty) * 0.9)
        }
        const topColors = sorted.slice(0, Math.min(10, sorted.length))
        let accent = topColors.reduce((best, c) => accentScore(c) > accentScore(best) ? c : best)
        // If top colors are all washed out, search the full palette
        if (accentScore(accent) < 30) {
          accent = sorted.reduce((best, c) => accentScore(c) > accentScore(best) ? c : best)
        }
        const accentColor = vibrantAccent(accent.r, accent.g, accent.b)

        resolve({ dominant, accent: accentColor })
      } catch {
        resolve(null)
      }
    }
    img.onerror = () => resolve(null)
    img.src = src
  })
}
