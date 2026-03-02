import type { SegmentationResult } from '../types'

const MAX_UNDO = 30

export interface BrushState {
  alpha: Uint8Array
  artRGBA: Uint8ClampedArray
  width: number
  height: number
  brushRadius: number
  mode: 'paint' | 'erase'
  undoStack: Uint8Array[]
  redoStack: Uint8Array[]
}

/** Load art pixels from an image source URL into an RGBA buffer */
async function loadArtPixels(artSrc: string, w: number, h: number): Promise<Uint8ClampedArray> {
  const img = new Image()
  img.crossOrigin = 'anonymous'
  await new Promise<void>((resolve, reject) => {
    img.onload = () => resolve()
    img.onerror = () => reject(new Error('Failed to load art image'))
    img.src = artSrc
  })
  const canvas = document.createElement('canvas')
  canvas.width = w
  canvas.height = h
  const ctx = canvas.getContext('2d')!
  ctx.drawImage(img, 0, 0, w, h)
  return ctx.getImageData(0, 0, w, h).data
}

/** Create a BrushState from an existing segmentation result */
export async function createBrushState(
  segmentation: SegmentationResult,
  artSrc: string,
): Promise<BrushState> {
  const { width, height, foregroundMask } = segmentation
  const alpha = new Uint8Array(width * height)
  const maskData = foregroundMask.data
  // Extract alpha channel from RGBA mask
  for (let i = 0; i < width * height; i++) {
    alpha[i] = maskData[i * 4 + 3]!
  }
  const artRGBA = await loadArtPixels(artSrc, width, height)
  return {
    alpha,
    artRGBA,
    width,
    height,
    brushRadius: 12,
    mode: 'paint',
    undoStack: [],
    redoStack: [],
  }
}

/** Create a blank BrushState (all foreground or all background) */
export async function createBlankBrushState(
  artSrc: string,
  w: number,
  h: number,
  fillAlpha = 255,
): Promise<BrushState> {
  const alpha = new Uint8Array(w * h)
  if (fillAlpha > 0) alpha.fill(fillAlpha)
  const artRGBA = await loadArtPixels(artSrc, w, h)
  return {
    alpha,
    artRGBA,
    width: w,
    height: h,
    brushRadius: 12,
    mode: 'paint',
    undoStack: [],
    redoStack: [],
  }
}

/** Stamp a circular brush at mask-space coordinates */
export function paintPoint(state: BrushState, x: number, y: number): void {
  const { alpha, width, height, brushRadius, mode } = state
  const value = mode === 'paint' ? 255 : 0
  const r = brushRadius
  const r2 = r * r

  const x0 = Math.max(0, Math.floor(x - r))
  const y0 = Math.max(0, Math.floor(y - r))
  const x1 = Math.min(width - 1, Math.ceil(x + r))
  const y1 = Math.min(height - 1, Math.ceil(y + r))

  for (let py = y0; py <= y1; py++) {
    for (let px = x0; px <= x1; px++) {
      const dx = px - x
      const dy = py - y
      if (dx * dx + dy * dy <= r2) {
        alpha[py * width + px] = value
      }
    }
  }
}

/** Interpolate between two points, stamping along the way */
export function paintLine(state: BrushState, x0: number, y0: number, x1: number, y1: number): void {
  const dx = x1 - x0
  const dy = y1 - y0
  const dist = Math.sqrt(dx * dx + dy * dy)
  const spacing = Math.max(1, state.brushRadius / 2)
  const steps = Math.max(1, Math.ceil(dist / spacing))

  for (let i = 0; i <= steps; i++) {
    const t = i / steps
    paintPoint(state, x0 + dx * t, y0 + dy * t)
  }
}

/** Push current alpha to undo stack and clear redo */
export function commitStroke(state: BrushState): void {
  state.undoStack.push(state.alpha.slice())
  if (state.undoStack.length > MAX_UNDO) {
    state.undoStack.shift()
  }
  state.redoStack.length = 0
}

/** Undo last stroke */
export function undo(state: BrushState): boolean {
  const snapshot = state.undoStack.pop()
  if (!snapshot) return false
  state.redoStack.push(state.alpha.slice())
  state.alpha.set(snapshot)
  return true
}

/** Redo last undone stroke */
export function redo(state: BrushState): boolean {
  const snapshot = state.redoStack.pop()
  if (!snapshot) return false
  state.undoStack.push(state.alpha.slice())
  state.alpha.set(snapshot)
  return true
}

/** Compose the artRGBA + alpha into a SegmentationResult */
export function composeMask(state: BrushState): SegmentationResult {
  const { artRGBA, alpha, width, height } = state
  const data = new Uint8ClampedArray(width * height * 4)

  for (let i = 0; i < width * height; i++) {
    data[i * 4] = artRGBA[i * 4]!
    data[i * 4 + 1] = artRGBA[i * 4 + 1]!
    data[i * 4 + 2] = artRGBA[i * 4 + 2]!
    data[i * 4 + 3] = alpha[i]!
  }

  return {
    foregroundMask: new ImageData(data, width, height),
    depthMap: null,
    width,
    height,
  }
}

/** Save the initial alpha state before any painting begins (for first undo) */
export function saveInitialState(state: BrushState): void {
  if (state.undoStack.length === 0) {
    state.undoStack.push(state.alpha.slice())
  }
}

/** Reset alpha to the initial state (before any painting), clear undo/redo stacks */
export function resetToInitial(state: BrushState): boolean {
  const initial = state.undoStack[0]
  if (!initial) return false
  state.alpha.set(initial)
  state.undoStack.length = 0
  state.redoStack.length = 0
  state.undoStack.push(state.alpha.slice())
  return true
}
