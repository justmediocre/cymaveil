import type { SegmentationBackend, MaskModelParams } from '../../types'
import type { SegmentationBackendModule } from './types'
import type { WorkerRequest, WorkerResponse } from './segmentation.worker'
import { depthToMask, loadImagePixels } from './depthToMask'
import { computeSaliencyMap, CLASSICAL_WORK_SIZE } from './classicalSaliency'

// ── Priority mutex ──────────────────────────────────────────────────────────
// Serialises backend access so only one caller uses the model at a time. When the
// lock frees, waiting foreground jobs (the playing cover, the mask editor) go before
// background ones (the library batch). The batch takes the lock once per cover, so
// the user never waits for more than the cover already in progress.

export type BackendPriority = 'foreground' | 'background'

let locked = false
const waiting: { priority: BackendPriority; grant: () => void }[] = []

function acquireLock(priority: BackendPriority): Promise<void> {
  if (!locked) {
    locked = true
    return Promise.resolve()
  }
  return new Promise(grant => waiting.push({ priority, grant }))
}

function releaseLock() {
  const fg = waiting.findIndex(w => w.priority === 'foreground')
  const next = waiting.splice(fg === -1 ? 0 : fg, 1)[0]
  // Hand the lock straight to the next job, or free it
  if (next) next.grant()
  else locked = false
}

// ── Worker lifecycle ────────────────────────────────────────────────────────
// One worker is kept alive across jobs so the model isn't reloaded for every cover.
// It is terminated once idle — terminating is the only way to free the ~94MB of WASM
// memory that pipeline.dispose() cannot reclaim.

const WORKER_IDLE_MS = 30_000

interface Pending {
  resolve: (res: WorkerResponse) => void
  reject: (err: Error) => void
}

interface WorkerHandle {
  worker: Worker
  config: string
  pending: Map<number, Pending>
}

let current: WorkerHandle | null = null
let idleTimer: ReturnType<typeof setTimeout> | null = null
let nextRequestId = 0

function createWorker(): Worker {
  return new Worker(
    new URL('./segmentation.worker.ts', import.meta.url),
    { type: 'module' },
  )
}

function configKey(params: MaskModelParams): string {
  return `${params.modelSize}:${params.modelDtype}`
}

/** Terminate the worker and fail anything still waiting on it, so no caller hangs */
function disposeWorker() {
  if (!current) return
  const { worker, pending } = current
  current = null
  worker.terminate()
  for (const p of pending.values()) p.reject(new Error('Segmentation worker terminated'))
  pending.clear()
}

function scheduleIdleDispose() {
  if (idleTimer) clearTimeout(idleTimer)
  idleTimer = setTimeout(() => {
    idleTimer = null
    if (!locked) disposeWorker()
  }, WORKER_IDLE_MS)
}

/** The worker for these model params, creating it (and loading the model) if needed */
async function getWorker(
  params: MaskModelParams,
  onProgress: ((p: number) => void) | null,
): Promise<WorkerHandle> {
  const config = configKey(params)
  if (current?.config === config) return current

  // Different model — replace the worker rather than reload in place, to free its memory
  disposeWorker()
  const handle: WorkerHandle = { worker: createWorker(), config, pending: new Map() }
  handle.worker.addEventListener('message', (e: MessageEvent<WorkerResponse>) => {
    const res = e.data
    if (!('id' in res) || res.id == null) return
    const p = handle.pending.get(res.id)
    if (!p) return
    handle.pending.delete(res.id)
    if (res.type === 'error') p.reject(new Error(res.message))
    else p.resolve(res)
  })
  handle.worker.addEventListener('error', () => {
    if (current === handle) disposeWorker()
  })
  current = handle

  try {
    await sendLoadModel(handle.worker, params, onProgress)
  } catch (err) {
    if (current === handle) disposeWorker()
    throw err
  }
  return handle
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
    } satisfies WorkerRequest)
  })
}

function request(
  handle: WorkerHandle,
  build: (id: number) => WorkerRequest,
  transfer: Transferable[] = [],
): Promise<WorkerResponse> {
  const id = nextRequestId++
  return new Promise((resolve, reject) => {
    handle.pending.set(id, { resolve, reject })
    handle.worker.postMessage(build(id), transfer)
  })
}

// ── Backend proxies ─────────────────────────────────────────────────────────

const classicalBackend: SegmentationBackendModule = {
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

  async segment(imageSrc, w, h, params) {
    try {
      const size = Math.min(w, h, CLASSICAL_WORK_SIZE)
      const { saliencyMap, width, height } = await computeSaliencyMap(imageSrc, size, size)
      return await depthToMask(saliencyMap, imageSrc, width, height, true, params)
    } catch (err) {
      if (import.meta.env.DEV) console.error('[classical] Segmentation failed:', err)
      return null
    }
  },

  dispose() {},
}

function workerBackend(handle: WorkerHandle): SegmentationBackendModule {
  return {
    name: 'Depth Anything v2',
    modelSize: '~25 MB',

    isLoaded: () => true,
    load: async () => {},
    loadWithParams: async () => {},

    async estimateDepth(imageSrc, width, height) {
      try {
        const res = await request(handle, id => ({ type: 'estimateDepth', id, imageSrc, width, height }))
        if (res.type !== 'depthResult') return null
        return { depthMap: new Uint8Array(res.depthMap), width: res.width, height: res.height }
      } catch (err) {
        if (import.meta.env.DEV) console.error('[depth-anything] Depth estimation failed:', err)
        return null
      }
    },

    // Inference and mask post-processing both run in the worker; only the cover
    // decode happens here (see loadImagePixels for why)
    async segment(imageSrc, width, height, params) {
      try {
        const pixels = (await loadImagePixels(imageSrc, width, height)).buffer as ArrayBuffer
        const res = await request(
          handle,
          id => ({ type: 'segment', id, imageSrc, pixels, width, height, params }),
          [pixels],
        )
        if (res.type !== 'segmentResult') return null
        return {
          foregroundMask: new ImageData(new Uint8ClampedArray(res.mask), res.width, res.height),
          depthMap: new Uint8Array(res.depthMap),
          width: res.width,
          height: res.height,
        }
      } catch (err) {
        if (import.meta.env.DEV) console.error('[depth-anything] Segmentation failed:', err)
        return null
      }
    },

    dispose() {
      // no-op — the worker is disposed once idle
    },
  }
}

// ── Public API ──────────────────────────────────────────────────────────────

/**
 * Scoped backend access: wait for the lock (foreground jobs first) → get the worker,
 * loading the model if needed → run workFn → release. The worker outlives the call
 * and is terminated after WORKER_IDLE_MS without work.
 */
export async function withBackend<T>(
  id: SegmentationBackend,
  params: MaskModelParams,
  onProgress: ((p: number) => void) | null,
  workFn: (backend: SegmentationBackendModule) => Promise<T>,
  priority: BackendPriority = 'foreground',
): Promise<T | null> {
  if (id === 'none') return null

  await acquireLock(priority)
  if (idleTimer) {
    clearTimeout(idleTimer)
    idleTimer = null
  }

  try {
    // Classical saliency backend — no worker, no download
    if (id === 'classical') return await workFn(classicalBackend)

    // ML backend (Depth Anything v2) — worker + WASM
    const handle = await getWorker(params, onProgress)
    return await workFn(workerBackend(handle))
  } finally {
    releaseLock()
    if (current) scheduleIdleDispose()
  }
}
