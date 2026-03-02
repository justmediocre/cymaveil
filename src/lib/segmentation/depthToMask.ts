import type { SegmentationResult, MaskPostProcessParams } from '../../types'
import { luminance } from '../imageUtils'

/** Built-in default post-processing parameters */
export const DEFAULT_MASK_PARAMS: MaskPostProcessParams = {
  bilateralRadius: 4,
  bilateralSigmaRange: 30,
  morphCloseRadius: 3,
  morphOpenRadius: 2,
  textPromotionRadius: 1,
  textPromotionSensitivity: 50,
  edgeRefineRadius: 3,
  featherRadius: 2,
}

// ---------------------------------------------------------------------------
// Reusable buffers — allocated once, grown as needed, reused across calls
// ---------------------------------------------------------------------------

let _morphBuf: Uint8Array | null = null
let _copyBuf: Uint8Array | null = null
let _featherBuf: Uint8Array | null = null

function getMorphBuf(size: number): Uint8Array {
  if (!_morphBuf || _morphBuf.length < size) _morphBuf = new Uint8Array(size)
  return _morphBuf
}

function getCopyBuf(size: number): Uint8Array {
  if (!_copyBuf || _copyBuf.length < size) _copyBuf = new Uint8Array(size)
  return _copyBuf
}

function getFeatherBuf(size: number): Uint8Array {
  if (!_featherBuf || _featherBuf.length < size) _featherBuf = new Uint8Array(size)
  return _featherBuf
}

/**
 * Find the value at a given percentile using a histogram (O(n) vs O(n log n) sort).
 * Replaces Float32Array.from(data).sort() which allocates a 256KB+ sorted copy.
 */
function histogramPercentile(data: Float32Array, percentile: number): number {
  const n = data.length
  if (n === 0) return 0

  let max = 0
  for (let i = 0; i < n; i++) {
    if (data[i]! > max) max = data[i]!
  }
  if (max === 0) return 0

  const BINS = 1024
  const histogram = new Uint32Array(BINS)
  const scale = (BINS - 1) / max

  for (let i = 0; i < n; i++) {
    histogram[Math.min(Math.floor(data[i]! * scale), BINS - 1)]!++
  }

  const target = Math.floor(n * percentile)
  let count = 0
  for (let i = 0; i < BINS; i++) {
    count += histogram[i]!
    if (count >= target) return (i + 0.5) / scale
  }
  return max
}

/**
 * Converts a depth map into a foreground mask (ImageData).
 *
 * Pipeline:
 *  1. Median filter — remove salt-and-pepper noise from raw depth
 *  2. Bilateral filter — smooth depth while preserving edges
 *  3. Otsu threshold — binary foreground/background split
 *  4. Text/logo promotion — recover text from background
 *  5. Edge-guided refinement — snap mask boundary to image edges
 *  6. Morphological close then open — fill holes, remove islands
 *  7. Anti-alias feather — thin Gaussian blur on the final alpha
 *
 * @param foregroundIsHigh - true if high depth values = close to camera (foreground).
 *   Depth Anything v2 outputs disparity (high = close), so pass true.
 * @param params - optional post-processing parameters (merged over defaults)
 */
export function depthToMask(
  depthMap: Uint8Array,
  imageSrc: string,
  width: number,
  height: number,
  foregroundIsHigh: boolean,
  params?: Partial<MaskPostProcessParams>,
): Promise<SegmentationResult> {
  const p = { ...DEFAULT_MASK_PARAMS, ...params }

  return new Promise((resolve, reject) => {
    const img = new Image()
    img.crossOrigin = 'anonymous'
    img.onload = () => {
      try {
        // Get original pixel data at target resolution
        const canvas = new OffscreenCanvas(width, height)
        const ctx = canvas.getContext('2d')!
        ctx.drawImage(img, 0, 0, width, height)
        const originalPixels = ctx.getImageData(0, 0, width, height)

        // 1. Median filter (3x3) — remove impulse noise
        const denoised = medianFilter(depthMap, width, height)

        // 2. Bilateral filter — smooth depth, preserve edges
        const bilateralSigmaSpace = p.bilateralRadius * 5
        const smoothed = bilateralFilter(denoised, width, height, p.bilateralRadius, bilateralSigmaSpace, p.bilateralSigmaRange)

        // 3. Otsu threshold → binary mask
        const threshold = otsuThreshold(smoothed)
        const alphaMap = new Uint8Array(width * height)
        for (let i = 0; i < smoothed.length; i++) {
          const isForeground = foregroundIsHigh
            ? smoothed[i]! >= threshold
            : smoothed[i]! <= threshold
          alphaMap[i] = isForeground ? 255 : 0
        }

        // Compute image gradient once — shared by text promotion and edge refinement
        const needGrad = p.textPromotionRadius > 0 || p.edgeRefineRadius > 0
        const imageGrad = needGrad ? computeImageGradient(originalPixels.data, width, height) : null

        // 4. Text/logo promotion — recover high-density gradient regions from background
        if (p.textPromotionRadius > 0 && imageGrad) {
          textPromotion(alphaMap, imageGrad.grad, width, height, p.textPromotionRadius, p.textPromotionSensitivity)
        }

        // 5. Edge-guided refinement — nudge mask edges toward strong image gradients
        if (p.edgeRefineRadius > 0) {
          edgeRefineWithGrad(alphaMap, imageGrad!, width, height, p.edgeRefineRadius)
        }

        // 6. Morphological close (fill holes) then open (remove islands)
        // Runs after edge refinement so cleanup isn't undone by boundary snapping
        if (p.morphCloseRadius > 0) morphClose(alphaMap, width, height, p.morphCloseRadius)
        if (p.morphOpenRadius > 0) morphOpen(alphaMap, width, height, p.morphOpenRadius)

        // 7. Anti-alias feather
        if (p.featherRadius > 0) gaussianFeather(alphaMap, width, height, p.featherRadius)

        // Compose foreground mask: RGB from original, A from alphaMap
        const maskData = new Uint8ClampedArray(width * height * 4)
        for (let i = 0; i < width * height; i++) {
          const si = i * 4
          maskData[si] = originalPixels.data[si]!
          maskData[si + 1] = originalPixels.data[si + 1]!
          maskData[si + 2] = originalPixels.data[si + 2]!
          maskData[si + 3] = alphaMap[i]!
        }

        resolve({
          foregroundMask: new ImageData(maskData, width, height),
          depthMap,
          width,
          height,
        })
      } catch (err) {
        reject(err)
      }
    }
    img.onerror = () => reject(new Error('Failed to load image for depth mask'))
    img.src = imageSrc
  })
}

// ---------------------------------------------------------------------------
// 1. Median filter (3×3)
// ---------------------------------------------------------------------------

function medianFilter(src: Uint8Array, w: number, h: number): Uint8Array {
  const out = new Uint8Array(w * h)
  const buf: number[] = new Array(9)
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      let n = 0
      for (let dy = -1; dy <= 1; dy++) {
        const ny = Math.min(Math.max(y + dy, 0), h - 1)
        for (let dx = -1; dx <= 1; dx++) {
          const nx = Math.min(Math.max(x + dx, 0), w - 1)
          buf[n++] = src[ny * w + nx]!
        }
      }
      buf.sort((a, b) => a - b)
      out[y * w + x] = buf[4]! // median of 9
    }
  }
  return out
}

// ---------------------------------------------------------------------------
// 2. Bilateral filter (edge-preserving smooth)
// ---------------------------------------------------------------------------

function bilateralFilter(
  src: Uint8Array, w: number, h: number,
  radius: number, sigmaSpace: number, sigmaRange: number,
): Uint8Array {
  const out = new Uint8Array(w * h)
  const ss2 = 2 * sigmaSpace * sigmaSpace
  const sr2 = 2 * sigmaRange * sigmaRange

  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const center = src[y * w + x]!
      let sum = 0
      let wSum = 0
      for (let dy = -radius; dy <= radius; dy++) {
        const ny = Math.min(Math.max(y + dy, 0), h - 1)
        for (let dx = -radius; dx <= radius; dx++) {
          const nx = Math.min(Math.max(x + dx, 0), w - 1)
          const val = src[ny * w + nx]!
          const dist2 = dx * dx + dy * dy
          const diff = val - center
          const weight = Math.exp(-dist2 / ss2 - (diff * diff) / sr2)
          sum += val * weight
          wSum += weight
        }
      }
      out[y * w + x] = Math.round(sum / wSum)
    }
  }
  return out
}

// ---------------------------------------------------------------------------
// 3. Otsu threshold
// ---------------------------------------------------------------------------

function otsuThreshold(data: Uint8Array): number {
  const histogram = new Uint32Array(256)
  for (let i = 0; i < data.length; i++) {
    histogram[data[i]!]!++
  }

  const total = data.length
  let sumAll = 0
  for (let i = 0; i < 256; i++) sumAll += i * histogram[i]!

  let sumBg = 0
  let weightBg = 0
  let maxVariance = 0
  let bestThreshold = 0

  for (let t = 0; t < 256; t++) {
    weightBg += histogram[t]!
    if (weightBg === 0) continue
    const weightFg = total - weightBg
    if (weightFg === 0) break

    sumBg += t * histogram[t]!
    const meanBg = sumBg / weightBg
    const meanFg = (sumAll - sumBg) / weightFg

    const diff = meanBg - meanFg
    const variance = weightBg * weightFg * diff * diff

    if (variance > maxVariance) {
      maxVariance = variance
      bestThreshold = t
    }
  }

  return bestThreshold
}

// ---------------------------------------------------------------------------
// 4. Morphological operations (binary, circular structuring element)
// ---------------------------------------------------------------------------

function dilate(mask: Uint8Array, w: number, h: number, r: number) {
  const n = w * h
  const out = getMorphBuf(n)
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      let found = false
      for (let dy = -r; dy <= r && !found; dy++) {
        const ny = y + dy
        if (ny < 0 || ny >= h) continue
        for (let dx = -r; dx <= r && !found; dx++) {
          if (dx * dx + dy * dy > r * r) continue
          const nx = x + dx
          if (nx < 0 || nx >= w) continue
          if (mask[ny * w + nx]! > 0) found = true
        }
      }
      out[y * w + x] = found ? 255 : 0
    }
  }
  for (let i = 0; i < n; i++) mask[i] = out[i]!
}

function erode(mask: Uint8Array, w: number, h: number, r: number) {
  const n = w * h
  const out = getMorphBuf(n)
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      let allSet = true
      for (let dy = -r; dy <= r && allSet; dy++) {
        const ny = y + dy
        if (ny < 0 || ny >= h) { allSet = false; continue }
        for (let dx = -r; dx <= r && allSet; dx++) {
          if (dx * dx + dy * dy > r * r) continue
          const nx = x + dx
          if (nx < 0 || nx >= w) { allSet = false; continue }
          if (mask[ny * w + nx]! === 0) allSet = false
        }
      }
      out[y * w + x] = allSet ? 255 : 0
    }
  }
  for (let i = 0; i < n; i++) mask[i] = out[i]!
}

/** Close = dilate then erode — fills small holes in foreground */
function morphClose(mask: Uint8Array, w: number, h: number, r: number) {
  dilate(mask, w, h, r)
  erode(mask, w, h, r)
}

/** Open = erode then dilate — removes small foreground islands */
function morphOpen(mask: Uint8Array, w: number, h: number, r: number) {
  erode(mask, w, h, r)
  dilate(mask, w, h, r)
}

// ---------------------------------------------------------------------------
// Shared image gradient computation (Sobel on luminance)
// ---------------------------------------------------------------------------

interface ImageGradient {
  lum: Float32Array
  grad: Float32Array
}

function computeImageGradient(rgba: Uint8ClampedArray, w: number, h: number): ImageGradient {
  const lum = new Float32Array(w * h)
  for (let i = 0; i < w * h; i++) {
    const ri = i * 4
    lum[i] = luminance(rgba[ri]!, rgba[ri + 1]!, rgba[ri + 2]!)
  }

  const grad = new Float32Array(w * h)
  for (let y = 1; y < h - 1; y++) {
    for (let x = 1; x < w - 1; x++) {
      const gx =
        -lum[(y - 1) * w + x - 1]! + lum[(y - 1) * w + x + 1]! +
        -2 * lum[y * w + x - 1]! + 2 * lum[y * w + x + 1]! +
        -lum[(y + 1) * w + x - 1]! + lum[(y + 1) * w + x + 1]!
      const gy =
        -lum[(y - 1) * w + x - 1]! - 2 * lum[(y - 1) * w + x]! - lum[(y - 1) * w + x + 1]! +
        lum[(y + 1) * w + x - 1]! + 2 * lum[(y + 1) * w + x]! + lum[(y + 1) * w + x + 1]!
      grad[y * w + x] = Math.sqrt(gx * gx + gy * gy)
    }
  }

  return { lum, grad }
}

// ---------------------------------------------------------------------------
// 4.5. Text/logo promotion
// ---------------------------------------------------------------------------

/**
 * Recover text and logo regions from background by detecting areas of high
 * local gradient density. Text has many strong edges packed closely together,
 * while smooth background regions have few.
 */
function textPromotion(
  alpha: Uint8Array,
  grad: Float32Array,
  w: number, h: number,
  radius: number,
  sensitivity: number,
) {
  const PROMOTION_ALPHA = 224
  const n = w * h
  const t = sensitivity / 100 // normalised 0..1

  // Scale thresholds with sensitivity so low values are genuinely strict
  const MIN_DENSITY = 0.35 - t * 0.20          // 0.35 (strict) → 0.15 (loose)
  const MIN_COMPONENT_SIZE = 50 - t * 38        // 50   (strict) → 12   (loose)

  // 1. Threshold gradient into binary edge map
  //    sensitivity 0 → percentile 0.97 (strict), 100 → 0.70 (loose)
  const percentile = 0.97 - t * 0.27
  const thresh = histogramPercentile(grad, percentile)

  const edgeBin = new Uint8Array(n)
  for (let i = 0; i < n; i++) {
    edgeBin[i] = grad[i]! >= thresh ? 1 : 0
  }

  // 2. Summed area table over binary edge map
  const sat = new Int32Array(n)
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = y * w + x
      sat[i] = edgeBin[i]!
        + (x > 0 ? sat[i - 1]! : 0)
        + (y > 0 ? sat[i - w]! : 0)
        - (x > 0 && y > 0 ? sat[i - w - 1]! : 0)
    }
  }

  // Helper: query sum in rectangle [x0,y0]..[x1,y1] inclusive
  const rectSum = (x0: number, y0: number, x1: number, y1: number): number => {
    const br = sat[y1 * w + x1]!
    const tl = (x0 > 0 && y0 > 0) ? sat[(y0 - 1) * w + (x0 - 1)]! : 0
    const tr = (y0 > 0) ? sat[(y0 - 1) * w + x1]! : 0
    const bl = (x0 > 0) ? sat[y1 * w + (x0 - 1)]! : 0
    return br - tr - bl + tl
  }

  // 3. Flag background pixels in high-density regions
  const windowSize = 2 * radius + 1
  const windowArea = windowSize * windowSize
  const candidates = new Uint8Array(n) // 1 = candidate for promotion
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = y * w + x
      if (alpha[i]! !== 0) continue // only promote background pixels
      const x0 = Math.max(x - radius, 0)
      const y0 = Math.max(y - radius, 0)
      const x1 = Math.min(x + radius, w - 1)
      const y1 = Math.min(y + radius, h - 1)
      const density = rectSum(x0, y0, x1, y1) / windowArea
      if (density >= MIN_DENSITY) {
        candidates[i] = 1
      }
    }
  }

  // 4. Connected component filter — discard components smaller than MIN_COMPONENT_SIZE
  const visited = new Uint8Array(n)
  const queue: number[] = []
  for (let i = 0; i < n; i++) {
    if (candidates[i]! !== 1 || visited[i]!) continue

    // BFS flood fill
    const component: number[] = []
    queue.length = 0
    queue.push(i)
    visited[i] = 1
    let head = 0
    while (head < queue.length) {
      const ci = queue[head++]!
      component.push(ci)
      const cx = ci % w
      const cy = (ci - cx) / w
      // 4-connected neighbors
      const neighbors = [
        cy > 0 ? ci - w : -1,
        cy < h - 1 ? ci + w : -1,
        cx > 0 ? ci - 1 : -1,
        cx < w - 1 ? ci + 1 : -1,
      ]
      for (const ni of neighbors) {
        if (ni >= 0 && !visited[ni]! && candidates[ni]! === 1) {
          visited[ni] = 1
          queue.push(ni)
        }
      }
    }

    // Promote surviving components
    if (component.length >= MIN_COMPONENT_SIZE) {
      for (const ci of component) {
        alpha[ci] = PROMOTION_ALPHA
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 5. Edge-guided refinement (uses pre-computed gradient)
// ---------------------------------------------------------------------------

/**
 * For pixels near the mask boundary, check whether a nearby strong image edge
 * exists. If so, shift the mask boundary to align with it. This snaps the
 * depth-based boundary to the actual color/luminance edges in the image.
 */
function edgeRefineWithGrad(
  alpha: Uint8Array,
  imageGrad: ImageGradient,
  w: number, h: number,
  searchRadius: number,
) {
  const { grad } = imageGrad

  // Find gradient threshold (top 15% of gradient values = strong edges)
  const edgeThresh = histogramPercentile(grad, 0.85)

  // Find mask boundary pixels and snap to nearest strong image edge
  const n = w * h
  const copy = getCopyBuf(n)
  copy.set(alpha)
  for (let y = 1; y < h - 1; y++) {
    for (let x = 1; x < w - 1; x++) {
      const idx = y * w + x
      // Is this a boundary pixel? (has both fg and bg neighbors)
      const val = copy[idx]!
      let hasDiff = false
      if (copy[idx - 1]! !== val || copy[idx + 1]! !== val ||
          copy[idx - w]! !== val || copy[idx + w]! !== val) {
        hasDiff = true
      }
      if (!hasDiff) continue

      // Search for the strongest image edge nearby
      let bestGrad = grad[idx]!
      let bestX = x
      let bestY = y
      for (let dy = -searchRadius; dy <= searchRadius; dy++) {
        const ny = y + dy
        if (ny < 1 || ny >= h - 1) continue
        for (let dx = -searchRadius; dx <= searchRadius; dx++) {
          const nx = x + dx
          if (nx < 1 || nx >= w - 1) continue
          const g = grad[ny * w + nx]!
          if (g > bestGrad && g >= edgeThresh) {
            bestGrad = g
            bestX = nx
            bestY = ny
          }
        }
      }

      // If a stronger edge was found nearby, adopt its mask state
      // (this shifts the boundary toward the image edge)
      if (bestX !== x || bestY !== y) {
        alpha[idx] = copy[bestY * w + bestX]!
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 6. Gaussian feather (anti-alias)
// ---------------------------------------------------------------------------

function gaussianFeather(alpha: Uint8Array, width: number, height: number, radius: number) {
  const kernel = makeGaussianKernel(radius)
  const tmp = getFeatherBuf(width * height)

  // Horizontal pass
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      let sum = 0
      let wSum = 0
      for (let k = -radius; k <= radius; k++) {
        const sx = Math.min(Math.max(x + k, 0), width - 1)
        const w = kernel[k + radius]!
        sum += alpha[y * width + sx]! * w
        wSum += w
      }
      tmp[y * width + x] = Math.round(sum / wSum)
    }
  }

  // Vertical pass
  for (let x = 0; x < width; x++) {
    for (let y = 0; y < height; y++) {
      let sum = 0
      let wSum = 0
      for (let k = -radius; k <= radius; k++) {
        const sy = Math.min(Math.max(y + k, 0), height - 1)
        const w = kernel[k + radius]!
        sum += tmp[sy * width + x]! * w
        wSum += w
      }
      alpha[y * width + x] = Math.round(sum / wSum)
    }
  }
}

function makeGaussianKernel(radius: number): Float64Array {
  const size = radius * 2 + 1
  const kernel = new Float64Array(size)
  const sigma = radius / 2
  for (let i = 0; i < size; i++) {
    const x = i - radius
    kernel[i] = Math.exp(-(x * x) / (2 * sigma * sigma))
  }
  return kernel
}
