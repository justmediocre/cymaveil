import type { SegmentationBackend, MaskModelParams } from '../../types'
import type { SegmentationBackendModule, DepthEstimation } from './types'
import type { WorkerResponse } from './segmentation.worker'
import { depthToMask } from './depthToMask'
import { computeSaliencyMap, CLASSICAL_WORK_SIZE } from './classicalSaliency'

// ── Mutex: serialises all backend access so only one caller loads/uses the model at a time ──

let lockTail: Promise<void> = Promise.resolve()

function acquireLock(): Promise<() => void> {
  let release!: () => void
  const next = new Promise<void>(r => { release = r })
  const wait = lockTail
  lockTail = next
  return wait.then(() => release)
}

// ── Worker lifecycle ────────────────────────────────────────────────────────

let currentWorker: Worker | null = null
let nextRequestId = 0

function createWorker(): Worker {
  return new Worker(
    new URL('./segmentation.worker.ts', import.meta.url),
    { type: 'module' },
  )
}

function sendLoadModel(
  worker: Worker,
  params: MaskModelParams,
  onProgress: ((p: number) => void) | null,
): Promise<void> {
  return new Promise((resolve, reject) => {
    const handler = (e: MessageEvent<WorkerResponse>) => {
      switch (e.data.type) {
        case 'progress':
          onProgress?.(e.data.value)
          break
        case 'modelLoaded':
          worker.removeEventListener('message', handler)
          resolve()
          break
        case 'error':
          if (e.data.id == null) {
            worker.removeEventListener('message', handler)
            reject(new Error(e.data.message))
          }
          break
      }
    }
    worker.addEventListener('message', handler)
    worker.postMessage({
      type: 'loadModel',
      params: { modelSize: params.modelSize, modelDtype: params.modelDtype },
    })
  })
}

function sendEstimateDepth(
  worker: Worker,
  imageSrc: string,
  width: number,
  height: number,
): Promise<DepthEstimation | null> {
  const id = nextRequestId++
  return new Promise((resolve, reject) => {
    const handler = (e: MessageEvent<WorkerResponse>) => {
      if (e.data.type === 'depthResult' && e.data.id === id) {
        worker.removeEventListener('message', handler)
        resolve({
          depthMap: new Uint8Array(e.data.depthMap),
          width: e.data.width,
          height: e.data.height,
        })
      } else if (e.data.type === 'error' && e.data.id === id) {
        worker.removeEventListener('message', handler)
        reject(new Error(e.data.message))
      }
    }
    worker.addEventListener('message', handler)
    worker.postMessage({ type: 'estimateDepth', id, imageSrc, width, height })
  })
}

// ── Public API (unchanged signatures) ───────────────────────────────────────

export function disposeCurrentBackend() {
  if (currentWorker) {
    currentWorker.terminate()
    currentWorker = null
  }
}

/**
 * Scoped backend access: acquire lock → spawn worker → load model → run workFn → terminate worker → release.
 * Terminating the worker frees all WASM memory (~94MB) that pipeline.dispose() cannot reclaim.
 */
export async function withBackend<T>(
  id: SegmentationBackend,
  params: MaskModelParams,
  onProgress: ((p: number) => void) | null,
  workFn: (backend: SegmentationBackendModule) => Promise<T>,
): Promise<T | null> {
  if (id === 'none') return null

  // ── Classical saliency backend — no worker, no download ──────────────
  if (id === 'classical') {
    const release = await acquireLock()
    try {
      const proxy: SegmentationBackendModule = {
        name: 'Classical Saliency',
        modelSize: '',

        isLoaded: () => true,
        load: async () => {},
        loadWithParams: async () => {},

        async estimateDepth(imageSrc, w, h) {
          try {
            const size = Math.min(w, h, CLASSICAL_WORK_SIZE)
            const { saliencyMap, width, height } = await computeSaliencyMap(imageSrc, size, size)
            return { depthMap: saliencyMap, width, height }
          } catch (err) {
            if (import.meta.env.DEV) console.error('[classical] Saliency estimation failed:', err)
            return null
          }
        },

        async segment(imageSrc, w, h) {
          try {
            const size = Math.min(w, h, CLASSICAL_WORK_SIZE)
            const { saliencyMap, width, height } = await computeSaliencyMap(imageSrc, size, size)
            return depthToMask(saliencyMap, imageSrc, width, height, true)
          } catch (err) {
            if (import.meta.env.DEV) console.error('[classical] Segmentation failed:', err)
            return null
          }
        },

        dispose() {},
      }

      return await workFn(proxy)
    } finally {
      release()
    }
  }

  // ── ML backend (Depth Anything v2) — worker + WASM ──────────────────
  const release = await acquireLock()
  const worker = createWorker()
  currentWorker = worker

  try {
    await sendLoadModel(worker, params, onProgress)

    // Proxy that forwards estimateDepth to the worker, runs depthToMask on main thread
    const proxy: SegmentationBackendModule = {
      name: 'Depth Anything v2',
      modelSize: '~25 MB',

      isLoaded: () => true,
      load: async () => {},
      loadWithParams: async () => {},

      async estimateDepth(imageSrc, w, h) {
        try {
          return await sendEstimateDepth(worker, imageSrc, w, h)
        } catch (err) {
          if (import.meta.env.DEV) console.error('[depth-anything] Depth estimation failed:', err)
          return null
        }
      },

      async segment(imageSrc, w, h) {
        try {
          const estimation = await sendEstimateDepth(worker, imageSrc, w, h)
          if (!estimation) return null
          return depthToMask(estimation.depthMap, imageSrc, w, h, true)
        } catch (err) {
          if (import.meta.env.DEV) console.error('[depth-anything] Segmentation failed:', err)
          return null
        }
      },

      dispose() {
        // no-op — worker is terminated in the finally block
      },
    }

    return await workFn(proxy)
  } finally {
    worker.terminate()
    currentWorker = null
    release()
  }
}
