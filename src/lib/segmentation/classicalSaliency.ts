import { luminance } from '../imageUtils'

const WORK_SIZE = 256

/**
 * Classical saliency map: produces a pseudo-depth Uint8Array (high = foreground)
 * using only image processing — no ML model, no download, no worker.
 *
 * Algorithm:
 *  1. Load image to OffscreenCanvas at 256×256
 *  2. Sample border pixels to estimate background color
 *  3. Per-pixel Euclidean color distance from border average
 *  4. Center-weighted Gaussian bias (subjects tend to be centered)
 *  5. Sobel gradient magnitude on luminance as local contrast signal
 *  6. Combine: saliency = colorDist × centerBias × (1 + gradWeight × gradient)
 *  7. Normalize to 0–255
 */
export async function computeSaliencyMap(
  imageSrc: string,
  width: number,
  height: number,
): Promise<{ saliencyMap: Uint8Array; width: number; height: number }> {
  const rgba = await loadImageRGBA(imageSrc, width, height)
  const n = width * height

  // --- 1. Border color average ---
  const border = sampleBorderAverage(rgba, width, height)

  // --- 2. Per-pixel color distance from border ---
  const colorDist = new Float32Array(n)
  for (let i = 0; i < n; i++) {
    const ri = i * 4
    const dr = rgba[ri]! - border.r
    const dg = rgba[ri + 1]! - border.g
    const db = rgba[ri + 2]! - border.b
    colorDist[i] = Math.sqrt(dr * dr + dg * dg + db * db)
  }

  // --- 3. Center-weighted Gaussian bias ---
  const centerBias = new Float32Array(n)
  const cx = (width - 1) / 2
  const cy = (height - 1) / 2
  const sigmaX = width * 0.4
  const sigmaY = height * 0.4
  for (let y = 0; y < height; y++) {
    const dy = y - cy
    const yTerm = (dy * dy) / (2 * sigmaY * sigmaY)
    for (let x = 0; x < width; x++) {
      const dx = x - cx
      centerBias[y * width + x] = Math.exp(-(dx * dx) / (2 * sigmaX * sigmaX) - yTerm)
    }
  }

  // --- 4. Sobel gradient magnitude on luminance ---
  const lum = new Float32Array(n)
  for (let i = 0; i < n; i++) {
    const ri = i * 4
    lum[i] = luminance(rgba[ri]!, rgba[ri + 1]!, rgba[ri + 2]!)
  }

  const grad = new Float32Array(n)
  for (let y = 1; y < height - 1; y++) {
    for (let x = 1; x < width - 1; x++) {
      const gx =
        -lum[(y - 1) * width + x - 1]! + lum[(y - 1) * width + x + 1]! +
        -2 * lum[y * width + x - 1]! + 2 * lum[y * width + x + 1]! +
        -lum[(y + 1) * width + x - 1]! + lum[(y + 1) * width + x + 1]!
      const gy =
        -lum[(y - 1) * width + x - 1]! - 2 * lum[(y - 1) * width + x]! - lum[(y - 1) * width + x + 1]! +
        lum[(y + 1) * width + x - 1]! + 2 * lum[(y + 1) * width + x]! + lum[(y + 1) * width + x + 1]!
      grad[y * width + x] = Math.sqrt(gx * gx + gy * gy)
    }
  }

  // Normalize gradient to 0–1
  let maxGrad = 0
  for (let i = 0; i < n; i++) {
    if (grad[i]! > maxGrad) maxGrad = grad[i]!
  }
  if (maxGrad > 0) {
    const invMax = 1 / maxGrad
    for (let i = 0; i < n; i++) grad[i] = grad[i]! * invMax
  }

  // --- 5. Combine ---
  const gradWeight = 0.5
  const raw = new Float32Array(n)
  for (let i = 0; i < n; i++) {
    raw[i] = colorDist[i]! * centerBias[i]! * (1 + gradWeight * grad[i]!)
  }

  // --- 6. Normalize to 0–255 ---
  let maxRaw = 0
  for (let i = 0; i < n; i++) {
    if (raw[i]! > maxRaw) maxRaw = raw[i]!
  }

  const saliencyMap = new Uint8Array(n)
  if (maxRaw > 0) {
    const scale = 255 / maxRaw
    for (let i = 0; i < n; i++) {
      saliencyMap[i] = Math.min(255, Math.round(raw[i]! * scale))
    }
  }

  return { saliencyMap, width, height }
}

/** Default working resolution for the classical backend */
export const CLASSICAL_WORK_SIZE = WORK_SIZE

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function loadImageRGBA(
  imageSrc: string,
  width: number,
  height: number,
): Promise<Uint8ClampedArray> {
  return new Promise((resolve, reject) => {
    const img = new Image()
    img.crossOrigin = 'anonymous'
    img.onload = () => {
      const canvas = new OffscreenCanvas(width, height)
      const ctx = canvas.getContext('2d')!
      ctx.drawImage(img, 0, 0, width, height)
      resolve(ctx.getImageData(0, 0, width, height).data)
    }
    img.onerror = () => reject(new Error('Failed to load image for classical saliency'))
    img.src = imageSrc
  })
}

function sampleBorderAverage(
  rgba: Uint8ClampedArray,
  w: number,
  h: number,
): { r: number; g: number; b: number } {
  let rSum = 0, gSum = 0, bSum = 0, count = 0

  const addPixel = (x: number, y: number) => {
    const i = (y * w + x) * 4
    rSum += rgba[i]!
    gSum += rgba[i + 1]!
    bSum += rgba[i + 2]!
    count++
  }

  // Top and bottom rows
  for (let x = 0; x < w; x++) {
    addPixel(x, 0)
    addPixel(x, h - 1)
  }
  // Left and right columns (excluding corners already counted)
  for (let y = 1; y < h - 1; y++) {
    addPixel(0, y)
    addPixel(w - 1, y)
  }

  return {
    r: rSum / count,
    g: gSum / count,
    b: bSum / count,
  }
}
