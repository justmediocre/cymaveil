import type { SegmentationResult, SegmentationBackend } from '../../types'

const DB_NAME = 'cymaveil-segmentation-cache'
const DB_VERSION = 4
const STORE_NAME = 'masks'
const OVERRIDES_STORE = 'mask-overrides'
const COLORS_STORE = 'album-colors'
const CONTOUR_STORE = 'contour-data'
const MAX_IDB_ENTRIES = 200
const LRU_CAPACITY = 8
// Bump when the mask algorithm changes to invalidate stale entries
const ALGO_VERSION = 5

// --- Hash helper ---

export async function hashArtSrc(src: string): Promise<string> {
  // For large data URIs, use prefix + length to avoid hashing megabytes
  const input = src.length > 8192 ? `${src.slice(0, 512)}:${src.length}` : src
  const buf = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(input))
  return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, '0')).join('')
}

function cacheKey(hash: string, backend: SegmentationBackend): string {
  return `v${ALGO_VERSION}:${backend}:${hash}`
}

// --- In-memory LRU ---

interface LRUEntry {
  key: string
  result: SegmentationResult
}

const lruMap = new Map<string, LRUEntry>()
const lruOrder: string[] = []

function lruGet(key: string): SegmentationResult | null {
  const entry = lruMap.get(key)
  if (!entry) return null
  // Move to end (most recently used)
  const idx = lruOrder.indexOf(key)
  if (idx !== -1) lruOrder.splice(idx, 1)
  lruOrder.push(key)
  return entry.result
}

function lruPut(key: string, result: SegmentationResult) {
  if (lruMap.has(key)) {
    const idx = lruOrder.indexOf(key)
    if (idx !== -1) lruOrder.splice(idx, 1)
  }
  lruMap.set(key, { key, result })
  lruOrder.push(key)
  while (lruOrder.length > LRU_CAPACITY) {
    const evict = lruOrder.shift()!
    lruMap.delete(evict)
  }
}

// --- IndexedDB (singleton connection) ---

let dbInstance: IDBDatabase | null = null
let dbPending: Promise<IDBDatabase> | null = null

function openDBOnce(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, DB_VERSION)
    req.onupgradeneeded = () => {
      const db = req.result
      if (!db.objectStoreNames.contains(STORE_NAME)) {
        const store = db.createObjectStore(STORE_NAME, { keyPath: 'key' })
        store.createIndex('timestamp', 'timestamp')
      }
      if (!db.objectStoreNames.contains(OVERRIDES_STORE)) {
        db.createObjectStore(OVERRIDES_STORE, { keyPath: 'artHash' })
      }
      if (!db.objectStoreNames.contains(COLORS_STORE)) {
        const cs = db.createObjectStore(COLORS_STORE, { keyPath: 'key' })
        cs.createIndex('timestamp', 'timestamp')
      }
      if (!db.objectStoreNames.contains(CONTOUR_STORE)) {
        const ct = db.createObjectStore(CONTOUR_STORE, { keyPath: 'key' })
        ct.createIndex('timestamp', 'timestamp')
      }
    }
    req.onsuccess = () => {
      const db = req.result
      db.onclose = () => {
        // Browser forced a disconnect — clear so next getDB() reconnects
        dbInstance = null
        dbPending = null
      }
      resolve(db)
    }
    req.onerror = () => reject(req.error)
  })
}

function getDB(): Promise<IDBDatabase> {
  if (dbInstance) return Promise.resolve(dbInstance)
  if (dbPending) return dbPending
  dbPending = openDBOnce().then((db) => {
    dbInstance = db
    return db
  }).catch((err) => {
    dbPending = null
    throw err
  })
  return dbPending
}

export { getDB, OVERRIDES_STORE, COLORS_STORE, CONTOUR_STORE }

/**
 * Count unique art hashes with any user customization
 * (union of user-edited masks and parameter overrides).
 */
export async function countAllCustomized(): Promise<number> {
  try {
    const db = await getDB()
    const artHashes = new Set<string>()

    // Collect art hashes from user-edited masks
    // Key format: v${ALGO_VERSION}:${backend}:${hash}
    await new Promise<void>((resolve) => {
      const tx = db.transaction(STORE_NAME, 'readonly')
      const cursor = tx.objectStore(STORE_NAME).openCursor()
      cursor.onsuccess = () => {
        const c = cursor.result
        if (c) {
          const rec = c.value as IDBRecord
          if (rec.userEdited) {
            const parts = rec.key.split(':')
            if (parts.length >= 3) {
              artHashes.add(parts.slice(2).join(':'))
            }
          }
          c.continue()
        } else {
          resolve()
        }
      }
      cursor.onerror = () => resolve()
    })

    // Collect art hashes from overrides
    await new Promise<void>((resolve) => {
      const tx = db.transaction(OVERRIDES_STORE, 'readonly')
      const cursor = tx.objectStore(OVERRIDES_STORE).openCursor()
      cursor.onsuccess = () => {
        const c = cursor.result
        if (c) {
          artHashes.add((c.value as { artHash: string }).artHash)
          c.continue()
        } else {
          resolve()
        }
      }
      cursor.onerror = () => resolve()
    })

    return artHashes.size
  } catch {
    return 0
  }
}

interface IDBRecord {
  key: string
  backend: SegmentationBackend
  width: number
  height: number
  foregroundRGBA: ArrayBuffer
  depthMap: ArrayBuffer | null
  timestamp: number
  userEdited?: boolean
  paramsHash?: string
}

function resultToRecord(
  key: string,
  backend: SegmentationBackend,
  result: SegmentationResult,
  userEdited = false,
  paramsHash = '',
): IDBRecord {
  return {
    key,
    backend,
    width: result.width,
    height: result.height,
    foregroundRGBA: (result.foregroundMask.data.buffer as ArrayBuffer).slice(0),
    depthMap: result.depthMap ? (result.depthMap.buffer as ArrayBuffer).slice(0) : null,
    timestamp: Date.now(),
    userEdited,
    paramsHash,
  }
}

function recordToResult(record: IDBRecord): SegmentationResult {
  const data = new Uint8ClampedArray(record.foregroundRGBA)
  return {
    foregroundMask: new ImageData(data, record.width, record.height),
    depthMap: record.depthMap ? new Uint8Array(record.depthMap) : null,
    width: record.width,
    height: record.height,
  }
}

async function idbGet(key: string): Promise<SegmentationResult | null> {
  try {
    const db = await getDB()
    return new Promise((resolve) => {
      const tx = db.transaction(STORE_NAME, 'readonly')
      const store = tx.objectStore(STORE_NAME)
      const req = store.get(key)
      req.onsuccess = () => {
        if (req.result) {
          resolve(recordToResult(req.result))
        } else {
          resolve(null)
        }
      }
      req.onerror = () => resolve(null)
    })
  } catch {
    return null
  }
}

async function idbPut(key: string, backend: SegmentationBackend, result: SegmentationResult, userEdited = false, paramsHash = '') {
  try {
    const db = await getDB()
    const tx = db.transaction(STORE_NAME, 'readwrite')
    const store = tx.objectStore(STORE_NAME)
    store.put(resultToRecord(key, backend, result, userEdited, paramsHash))

    // Evict oldest non-user-edited entries if over limit
    const countReq = store.count()
    countReq.onsuccess = () => {
      if (countReq.result > MAX_IDB_ENTRIES) {
        const idx = store.index('timestamp')
        const evictCount = countReq.result - MAX_IDB_ENTRIES
        let deleted = 0
        const cursor = idx.openCursor()
        cursor.onsuccess = () => {
          const c = cursor.result
          if (c && deleted < evictCount) {
            const rec = c.value as IDBRecord
            if (!rec.userEdited) {
              c.delete()
              deleted++
            }
            c.continue()
          }
        }
      }
    }
  } catch (err: unknown) {
    // Cache write failure is non-fatal — log so quota issues are diagnosable
    if (import.meta.env.DEV) {
      const msg = err instanceof DOMException ? err.name : String(err)
      console.warn('[segmentation-cache] write failed:', msg)
    }
  }
}

async function idbGetRaw(key: string): Promise<IDBRecord | null> {
  try {
    const db = await getDB()
    return new Promise((resolve) => {
      const tx = db.transaction(STORE_NAME, 'readonly')
      const store = tx.objectStore(STORE_NAME)
      const req = store.get(key)
      req.onsuccess = () => resolve(req.result ?? null)
      req.onerror = () => resolve(null)
    })
  } catch {
    return null
  }
}

// --- Base64 helpers for export/import ---

function bufferToBase64(buf: ArrayBuffer): string {
  const bytes = new Uint8Array(buf)
  let binary = ''
  for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]!)
  return btoa(binary)
}

function base64ToBuffer(b64: string): ArrayBuffer {
  const binary = atob(b64)
  const bytes = new Uint8Array(binary.length)
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i)
  return bytes.buffer as ArrayBuffer
}

export interface UserEditedMaskExport {
  artHash: string
  backend: SegmentationBackend
  width: number
  height: number
  foregroundRGBA: string // base64
  depthMap: string | null // base64
}

// --- Public API ---

export const segmentationCache = {
  async get(artSrc: string, backend: SegmentationBackend): Promise<SegmentationResult | null> {
    const hash = await hashArtSrc(artSrc)
    const key = cacheKey(hash, backend)

    // Check in-memory first
    const mem = lruGet(key)
    if (mem) return mem

    // Check IndexedDB
    const idb = await idbGet(key)
    if (idb) {
      lruPut(key, idb)
      return idb
    }

    return null
  },

  async put(artSrc: string, backend: SegmentationBackend, result: SegmentationResult) {
    const hash = await hashArtSrc(artSrc)
    const key = cacheKey(hash, backend)

    lruPut(key, result)
    await idbPut(key, backend, result)
  },

  /** Store a user-edited mask (protected from auto-clear) */
  async putUserEdited(artSrc: string, backend: SegmentationBackend, result: SegmentationResult, paramsHash = '') {
    const hash = await hashArtSrc(artSrc)
    const key = cacheKey(hash, backend)

    lruPut(key, result)
    await idbPut(key, backend, result, true, paramsHash)
  },

  /** Get just the depth map from a cached entry (for re-processing without ML re-run) */
  async getDepthMap(artSrc: string, backend: SegmentationBackend): Promise<{ depthMap: Uint8Array; width: number; height: number } | null> {
    const hash = await hashArtSrc(artSrc)
    const key = cacheKey(hash, backend)

    // Check LRU first
    const mem = lruGet(key)
    if (mem?.depthMap) {
      return { depthMap: mem.depthMap, width: mem.width, height: mem.height }
    }

    // Check IDB raw record
    const record = await idbGetRaw(key)
    if (record?.depthMap) {
      return { depthMap: new Uint8Array(record.depthMap), width: record.width, height: record.height }
    }

    return null
  },

  /** Clear auto-generated cache (skips user-edited entries) */
  async clear() {
    // Clear in-memory LRU
    lruMap.clear()
    lruOrder.length = 0

    // Clear only non-user-edited entries from IndexedDB
    try {
      const db = await getDB()
      const tx = db.transaction(STORE_NAME, 'readwrite')
      const store = tx.objectStore(STORE_NAME)
      const cursor = store.openCursor()
      cursor.onsuccess = () => {
        const c = cursor.result
        if (c) {
          const rec = c.value as IDBRecord
          if (!rec.userEdited) {
            c.delete()
          }
          c.continue()
        }
      }
    } catch {
      // Non-fatal
    }
  },

  /** Clear only user-edited masks */
  async clearUserEdited() {
    try {
      const db = await getDB()
      const tx = db.transaction(STORE_NAME, 'readwrite')
      const store = tx.objectStore(STORE_NAME)
      const cursor = store.openCursor()
      cursor.onsuccess = () => {
        const c = cursor.result
        if (c) {
          const rec = c.value as IDBRecord
          if (rec.userEdited) {
            c.delete()
          }
          c.continue()
        }
      }
    } catch {
      // Non-fatal
    }

    // Also clear LRU since we don't track userEdited there
    lruMap.clear()
    lruOrder.length = 0
  },

  /** Clear everything (both auto-generated and user-edited) */
  async clearAll() {
    lruMap.clear()
    lruOrder.length = 0

    try {
      const db = await getDB()
      const tx = db.transaction(STORE_NAME, 'readwrite')
      tx.objectStore(STORE_NAME).clear()
    } catch {
      // Non-fatal
    }
  },

  /** Remove a specific cache entry (both LRU and IDB) for a given artSrc + backend */
  async removeEntry(artSrc: string, backend: SegmentationBackend): Promise<void> {
    const hash = await hashArtSrc(artSrc)
    const key = cacheKey(hash, backend)

    // Remove from LRU
    if (lruMap.has(key)) {
      lruMap.delete(key)
      const idx = lruOrder.indexOf(key)
      if (idx !== -1) lruOrder.splice(idx, 1)
    }

    // Remove from IDB
    try {
      const db = await getDB()
      const tx = db.transaction(STORE_NAME, 'readwrite')
      tx.objectStore(STORE_NAME).delete(key)
    } catch {
      // Non-fatal
    }
  },

  /** Check if a specific cached mask is user-edited */
  async isUserEdited(artSrc: string, backend: SegmentationBackend): Promise<boolean> {
    const hash = await hashArtSrc(artSrc)
    const key = cacheKey(hash, backend)
    const record = await idbGetRaw(key)
    return !!record?.userEdited
  },

  /** Count user-edited entries */
  async countUserEdited(): Promise<number> {
    try {
      const db = await getDB()
      return new Promise((resolve) => {
        const tx = db.transaction(STORE_NAME, 'readonly')
        const store = tx.objectStore(STORE_NAME)
        let count = 0
        const cursor = store.openCursor()
        cursor.onsuccess = () => {
          const c = cursor.result
          if (c) {
            const rec = c.value as IDBRecord
            if (rec.userEdited) count++
            c.continue()
          } else {
            resolve(count)
          }
        }
        cursor.onerror = () => resolve(0)
      })
    } catch {
      return 0
    }
  },

  /** Serialize all user-edited masks for JSON export */
  async exportUserEdited(): Promise<UserEditedMaskExport[]> {
    try {
      const db = await getDB()
      return new Promise((resolve) => {
        const results: UserEditedMaskExport[] = []
        const tx = db.transaction(STORE_NAME, 'readonly')
        const cursor = tx.objectStore(STORE_NAME).openCursor()
        cursor.onsuccess = () => {
          const c = cursor.result
          if (c) {
            const rec = c.value as IDBRecord
            if (rec.userEdited) {
              // Extract artHash from key: v${ALGO_VERSION}:${backend}:${hash}
              const parts = rec.key.split(':')
              if (parts.length >= 3) {
                results.push({
                  artHash: parts.slice(2).join(':'),
                  backend: rec.backend,
                  width: rec.width,
                  height: rec.height,
                  foregroundRGBA: bufferToBase64(rec.foregroundRGBA),
                  depthMap: rec.depthMap ? bufferToBase64(rec.depthMap) : null,
                })
              }
            }
            c.continue()
          } else {
            resolve(results)
          }
        }
        cursor.onerror = () => resolve([])
      })
    } catch {
      return []
    }
  },

  /** Restore user-edited masks from a JSON import */
  async importUserEdited(masks: UserEditedMaskExport[]): Promise<void> {
    if (masks.length === 0) return
    try {
      const db = await getDB()
      const tx = db.transaction(STORE_NAME, 'readwrite')
      const store = tx.objectStore(STORE_NAME)
      for (const mask of masks) {
        const key = cacheKey(mask.artHash, mask.backend)
        const record: IDBRecord = {
          key,
          backend: mask.backend,
          width: mask.width,
          height: mask.height,
          foregroundRGBA: base64ToBuffer(mask.foregroundRGBA),
          depthMap: mask.depthMap ? base64ToBuffer(mask.depthMap) : null,
          timestamp: Date.now(),
          userEdited: true,
        }
        store.put(record)
      }
    } catch {
      // Non-fatal
    }
  },
}
