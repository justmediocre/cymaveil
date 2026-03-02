/**
 * Web Worker for ONNX depth estimation.
 * Keeps the ~94MB WASM runtime (module + linear memory) off the main thread.
 * Terminating this worker frees all WASM memory — unlike pipeline.dispose()
 * which leaves the WASM module and SharedArrayBuffer in the V8 heap.
 */

// Worker self — typed as any to avoid DOM vs WebWorker lib conflicts.
// Vite handles the worker context correctly at runtime.
const ctx = self as any
export {} // ensure this file is treated as a module

// ── Message protocol ──────────────────────────────────────────────────────────

import type { DepthModelSize, DepthModelDtype } from '../../types'

export type WorkerRequest =
  | { type: 'loadModel'; params: { modelSize?: DepthModelSize; modelDtype?: DepthModelDtype } }
  | { type: 'estimateDepth'; id: number; imageSrc: string; width: number; height: number }
  | { type: 'dispose' }

export type WorkerResponse =
  | { type: 'progress'; value: number }
  | { type: 'modelLoaded' }
  | { type: 'depthResult'; id: number; depthMap: ArrayBuffer; width: number; height: number }
  | { type: 'error'; id?: number; message: string }

// ── Internal state ────────────────────────────────────────────────────────────

/** Minimal shape of the @huggingface/transformers pipeline callable. */
interface DepthPipeline {
  (input: string): Promise<DepthPipelineResult>
  dispose(): void
}

/** Shape of a single depth-estimation result from the pipeline. */
interface DepthPipelineResult {
  depth: {
    data: Float32Array | Uint8Array
    width: number
    height: number
    channels?: number
  }
}

/** Progress event emitted during model download. */
interface ProgressInfo {
  status: string
  progress?: number
}

let pipeline: DepthPipeline | null = null
let currentConfig: { modelSize: string; modelDtype: string } | null = null

const DEFAULT_MODEL_SIZE: DepthModelSize = 'small'
const DEFAULT_MODEL_DTYPE: DepthModelDtype = 'q8'

function modelId(size: DepthModelSize): string {
  return `onnx-community/depth-anything-v2-${size}`
}

async function detectDevice(): Promise<'webgpu' | 'wasm'> {
  if (typeof navigator !== 'undefined' && 'gpu' in navigator) {
    try {
      // WebGPU API — not yet in TypeScript's default lib.dom.d.ts
      const gpu = (navigator as Navigator & { gpu?: { requestAdapter(): Promise<unknown> } }).gpu
      const adapter = await gpu?.requestAdapter()
      if (adapter) return 'webgpu'
    } catch { /* WebGPU not usable */ }
  }
  return 'wasm'
}

async function createPipelineWithParams(
  size: DepthModelSize,
  dtype: DepthModelDtype,
  onProgress?: (progress: number) => void,
) {
  const { pipeline: createPipeline } = await import('@huggingface/transformers')
  const device = await detectDevice()

  return createPipeline('depth-estimation', modelId(size), {
    dtype,
    device,
    progress_callback: onProgress
      ? (info: ProgressInfo) => {
          if (info.status === 'progress' && info.progress != null) {
            onProgress(info.progress / 100)
          }
        }
      : undefined,
  }) as unknown as DepthPipeline
}

function extractDepthMap(result: DepthPipelineResult, width: number, height: number): Uint8Array | null {
  if (!result?.depth) return null

  const depthImage = result.depth
  const depthData = depthImage.data
  const srcW = depthImage.width
  const srcH = depthImage.height
  const channels = depthImage.channels ?? Math.round(depthData.length / (srcW * srcH))

  // Find min/max for normalization
  let minVal = Infinity
  let maxVal = -Infinity
  for (let i = 0; i < srcW * srcH; i++) {
    const v = depthData[i * channels]!
    if (v < minVal) minVal = v
    if (v > maxVal) maxVal = v
  }

  const range = maxVal - minVal
  const normalized = new Float32Array(srcW * srcH)
  for (let i = 0; i < srcW * srcH; i++) {
    const raw = depthData[i * channels]!
    normalized[i] = range > 0 ? ((raw - minVal) / range) * 255 : 128
  }

  // Bilinear interpolation resize to target dimensions
  const depthMap = new Uint8Array(width * height)
  for (let y = 0; y < height; y++) {
    const srcY = (y / height) * srcH - 0.5
    const y0 = Math.max(0, Math.floor(srcY))
    const y1 = Math.min(srcH - 1, y0 + 1)
    const fy = srcY - y0

    for (let x = 0; x < width; x++) {
      const srcX = (x / width) * srcW - 0.5
      const x0 = Math.max(0, Math.floor(srcX))
      const x1 = Math.min(srcW - 1, x0 + 1)
      const fx = srcX - x0

      const v00 = normalized[y0 * srcW + x0]!
      const v10 = normalized[y0 * srcW + x1]!
      const v01 = normalized[y1 * srcW + x0]!
      const v11 = normalized[y1 * srcW + x1]!

      const v = v00 * (1 - fx) * (1 - fy) +
                v10 * fx * (1 - fy) +
                v01 * (1 - fx) * fy +
                v11 * fx * fy

      depthMap[y * width + x] = Math.round(Math.min(255, Math.max(0, v)))
    }
  }

  return depthMap
}

// ── Message handler ───────────────────────────────────────────────────────────

ctx.onmessage = async (e: MessageEvent<WorkerRequest>) => {
  const msg = e.data

  switch (msg.type) {
    case 'loadModel': {
      try {
        const size = msg.params.modelSize || DEFAULT_MODEL_SIZE
        const dtype = msg.params.modelDtype || DEFAULT_MODEL_DTYPE

        // Already loaded with same config — skip
        if (pipeline && currentConfig?.modelSize === size && currentConfig?.modelDtype === dtype) {
          ctx.postMessage({ type: 'modelLoaded' } as WorkerResponse)
          return
        }

        // Dispose current pipeline before loading new one
        if (pipeline && typeof pipeline.dispose === 'function') {
          pipeline.dispose()
        }
        pipeline = null

        pipeline = await createPipelineWithParams(size, dtype, (p) => {
          ctx.postMessage({ type: 'progress', value: p } as WorkerResponse)
        })
        currentConfig = { modelSize: size, modelDtype: dtype }

        ctx.postMessage({ type: 'modelLoaded' } as WorkerResponse)
      } catch (err) {
        ctx.postMessage({
          type: 'error',
          message: (err as Error).message,
        } as WorkerResponse)
      }
      break
    }

    case 'estimateDepth': {
      try {
        if (!pipeline) {
          ctx.postMessage({
            type: 'error',
            id: msg.id,
            message: 'Model not loaded',
          } as WorkerResponse)
          return
        }

        const result = await pipeline(msg.imageSrc)
        const depthMap = extractDepthMap(result, msg.width, msg.height)

        if (!depthMap) {
          ctx.postMessage({
            type: 'error',
            id: msg.id,
            message: 'Depth estimation returned null',
          } as WorkerResponse)
          return
        }

        // Transfer the underlying ArrayBuffer (zero-copy)
        const buffer = depthMap.buffer as ArrayBuffer
        ctx.postMessage(
          {
            type: 'depthResult',
            id: msg.id,
            depthMap: buffer,
            width: msg.width,
            height: msg.height,
          } as WorkerResponse,
          [buffer],
        )
      } catch (err) {
        ctx.postMessage({
          type: 'error',
          id: msg.id,
          message: (err as Error).message,
        } as WorkerResponse)
      }
      break
    }

    case 'dispose': {
      if (pipeline && typeof pipeline.dispose === 'function') {
        pipeline.dispose()
      }
      pipeline = null
      currentConfig = null
      break
    }
  }
}
