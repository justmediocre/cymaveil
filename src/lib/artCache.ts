import type { AlbumColors, ContourData } from '../types'
import { getDB, hashArtSrc, COLORS_STORE, CONTOUR_STORE } from './segmentation/cache'

const COLORS_LRU_CAPACITY = 16
const CONTOUR_LRU_CAPACITY = 8
const MAX_IDB_ENTRIES = 200
const COLORS_VERSION = 'colors:v2'
const CONTOUR_VERSION = 'contour:v1'

// --- In-memory LRU ---

function createLRU<T>(capacity: number) {
  const map = new Map<string, T>()
  const order: string[] = []

  return {
    get(key: string): T | null {
      const val = map.get(key)
      if (val === undefined) return null
      const idx = order.indexOf(key)
      if (idx !== -1) order.splice(idx, 1)
      order.push(key)
      return val
    },
    put(key: string, value: T) {
      if (map.has(key)) {
        const idx = order.indexOf(key)
        if (idx !== -1) order.splice(idx, 1)
      }
      map.set(key, value)
      order.push(key)
      while (order.length > capacity) {
        const evict = order.shift()!
        map.delete(evict)
      }
    },
    clear() {
      map.clear()
      order.length = 0
    },
  }
}

const colorsLRU = createLRU<AlbumColors>(COLORS_LRU_CAPACITY)
const contourLRU = createLRU<ContourData>(CONTOUR_LRU_CAPACITY)

// --- IDB helpers ---

async function idbGet<T>(storeName: string, key: string): Promise<T | null> {
  try {
    const db = await getDB()
    return new Promise((resolve) => {
      const tx = db.transaction(storeName, 'readonly')
      const store = tx.objectStore(storeName)
      const req = store.get(key)
      req.onsuccess = () => {
        if (req.result) {
          resolve(req.result.data as T)
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

async function idbPut<T>(storeName: string, key: string, data: T) {
  try {
    const db = await getDB()
    const tx = db.transaction(storeName, 'readwrite')
    const store = tx.objectStore(storeName)
    store.put({ key, data, timestamp: Date.now() })

    // Evict oldest entries if over limit
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
            c.delete()
            deleted++
            c.continue()
          }
        }
      }
    }
  } catch {
    // Cache write failure is non-fatal
  }
}

async function idbClear(storeName: string) {
  try {
    const db = await getDB()
    const tx = db.transaction(storeName, 'readwrite')
    tx.objectStore(storeName).clear()
  } catch {
    // Non-fatal
  }
}

// --- Public API ---

export const artCache = {
  async getColors(artSrc: string): Promise<AlbumColors | null> {
    const hash = await hashArtSrc(artSrc)
    const key = `${COLORS_VERSION}:${hash}`

    const mem = colorsLRU.get(key)
    if (mem) return mem

    const idb = await idbGet<AlbumColors>(COLORS_STORE, key)
    if (idb) {
      colorsLRU.put(key, idb)
      return idb
    }

    return null
  },

  async putColors(artSrc: string, colors: AlbumColors) {
    const hash = await hashArtSrc(artSrc)
    const key = `${COLORS_VERSION}:${hash}`

    colorsLRU.put(key, colors)
    await idbPut(COLORS_STORE, key, colors)
  },

  async getContour(artSrc: string): Promise<ContourData | null> {
    const hash = await hashArtSrc(artSrc)
    const key = `${CONTOUR_VERSION}:${hash}`

    const mem = contourLRU.get(key)
    if (mem) return mem

    const idb = await idbGet<ContourData>(CONTOUR_STORE, key)
    if (idb) {
      contourLRU.put(key, idb)
      return idb
    }

    return null
  },

  async putContour(artSrc: string, contourData: ContourData) {
    const hash = await hashArtSrc(artSrc)
    const key = `${CONTOUR_VERSION}:${hash}`

    contourLRU.put(key, contourData)
    await idbPut(CONTOUR_STORE, key, contourData)
  },

  async clear() {
    colorsLRU.clear()
    contourLRU.clear()
    await idbClear(COLORS_STORE)
    await idbClear(CONTOUR_STORE)
  },
}
