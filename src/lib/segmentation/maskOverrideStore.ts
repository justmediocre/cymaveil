import type { MaskModelParams, MaskPostProcessParams } from '../../types'
import { getDB, OVERRIDES_STORE } from './cache'

export interface MaskOverrideRecord {
  artHash: string
  modelParams: MaskModelParams
  postProcessParams: MaskPostProcessParams
  timestamp: number
}

export const maskOverrideStore = {
  async get(artHash: string): Promise<{ modelParams: MaskModelParams; postProcessParams: MaskPostProcessParams } | null> {
    try {
      const db = await getDB()
      return new Promise((resolve) => {
        const tx = db.transaction(OVERRIDES_STORE, 'readonly')
        const store = tx.objectStore(OVERRIDES_STORE)
        const req = store.get(artHash)
        req.onsuccess = () => {
          const rec = req.result as MaskOverrideRecord | undefined
          if (rec) {
            resolve({ modelParams: rec.modelParams, postProcessParams: rec.postProcessParams })
          } else {
            resolve(null)
          }
        }
        req.onerror = () => resolve(null)
      })
    } catch {
      return null
    }
  },

  async put(artHash: string, modelParams: MaskModelParams, postProcessParams: MaskPostProcessParams): Promise<void> {
    try {
      const db = await getDB()
      const tx = db.transaction(OVERRIDES_STORE, 'readwrite')
      const store = tx.objectStore(OVERRIDES_STORE)
      const record: MaskOverrideRecord = {
        artHash,
        modelParams,
        postProcessParams,
        timestamp: Date.now(),
      }
      store.put(record)
    } catch {
      // Non-fatal
    }
  },

  async remove(artHash: string): Promise<void> {
    try {
      const db = await getDB()
      const tx = db.transaction(OVERRIDES_STORE, 'readwrite')
      tx.objectStore(OVERRIDES_STORE).delete(artHash)
    } catch {
      // Non-fatal
    }
  },

  async clearAll(): Promise<void> {
    try {
      const db = await getDB()
      const tx = db.transaction(OVERRIDES_STORE, 'readwrite')
      tx.objectStore(OVERRIDES_STORE).clear()
    } catch {
      // Non-fatal
    }
  },

  async count(): Promise<number> {
    try {
      const db = await getDB()
      return new Promise((resolve) => {
        const tx = db.transaction(OVERRIDES_STORE, 'readonly')
        const req = tx.objectStore(OVERRIDES_STORE).count()
        req.onsuccess = () => resolve(req.result)
        req.onerror = () => resolve(0)
      })
    } catch {
      return 0
    }
  },

  async getAll(): Promise<MaskOverrideRecord[]> {
    try {
      const db = await getDB()
      return new Promise((resolve) => {
        const tx = db.transaction(OVERRIDES_STORE, 'readonly')
        const req = tx.objectStore(OVERRIDES_STORE).getAll()
        req.onsuccess = () => resolve(req.result as MaskOverrideRecord[])
        req.onerror = () => resolve([])
      })
    } catch {
      return []
    }
  },

  async putBatch(records: MaskOverrideRecord[]): Promise<void> {
    try {
      const db = await getDB()
      const tx = db.transaction(OVERRIDES_STORE, 'readwrite')
      const store = tx.objectStore(OVERRIDES_STORE)
      for (const record of records) {
        store.put(record)
      }
    } catch {
      // Non-fatal
    }
  },
}
