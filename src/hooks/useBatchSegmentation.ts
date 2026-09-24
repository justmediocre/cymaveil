import { useState, useEffect, useRef, useCallback, useMemo } from 'react'
import type { Album, Track, MaskModelParams, MaskPostProcessParams } from '../types'
import { segmentationCache, hashArtSrc } from '../lib/segmentation/cache'
import { maskOverrideStore } from '../lib/segmentation/maskOverrideStore'
import { withBackend } from '../lib/segmentation/registry'
import { depthToMask, DEFAULT_MASK_PARAMS } from '../lib/segmentation/depthToMask'
import useVisualSettings from './useVisualSettings'
import { DEFAULT_MODEL_PARAMS } from './useSegmentation'

export interface BatchProgress {
  current: number
  total: number
}

export default function useBatchSegmentation(albums: Album[], tracks: Track[] = []): {
  processing: boolean
  progress: BatchProgress | null
  processAll: () => void
} {
  const { settings } = useVisualSettings()
  const [processing, setProcessing] = useState(false)
  const [progress, setProgress] = useState<BatchProgress | null>(null)
  const [runId, setRunId] = useState(0)

  const enabled = settings.depthLayerEnabled
  const backendId = settings.segmentationBackend

  // Read at processing time rather than restarting the whole batch when they change
  const maskDefaultsRef = useRef(settings.maskDefaults)
  maskDefaultsRef.current = settings.maskDefaults

  // Unique art sources: album covers plus per-track overrides. Joined into one
  // string so the batch only restarts when the set of covers changes, not every
  // time the albums/tracks arrays are replaced.
  // Base64 data URIs are skipped: the library save swaps them for artwork:// URLs
  // moments after an import, and a mask cached under the data URI is never read again.
  const artKey = useMemo(() => {
    const seen = new Set<string>()
    for (const src of [...albums.map(a => a.art), ...tracks.map(t => t.art ?? null)]) {
      if (src && !src.startsWith('data:')) seen.add(src)
    }
    return [...seen].join('\n')
  }, [albums, tracks])

  useEffect(() => {
    if (!enabled || backendId === 'none' || backendId === 'manual') return

    const artSources = artKey ? artKey.split('\n') : []
    if (artSources.length === 0) return

    // Per-run flag: a shared ref would be reset by the next run and revive this one
    let cancelled = false

    ;(async () => {
      // Find uncached art sources
      const uncached: { art: string; hash: string }[] = []
      for (const art of artSources) {
        if (cancelled) return
        const cached = await segmentationCache.get(art, backendId)
        if (!cached) {
          const hash = await hashArtSrc(art)
          uncached.push({ art, hash })
        }
      }

      // Never batch past the cache's capacity: each put beyond it evicts a mask this
      // same batch just made, and the next launch would find those uncached and redo
      // them, forever. Covers left over get a mask on demand when they're played.
      const room = segmentationCache.capacity - await segmentationCache.count()
      uncached.splice(Math.max(0, room))

      if (uncached.length === 0 || cancelled) return

      setProcessing(true)
      setProgress({ current: 0, total: uncached.length })

      await withBackend(backendId, DEFAULT_MODEL_PARAMS, null, async (backend) => {
        for (let i = 0; i < uncached.length; i++) {
          if (cancelled) return

          const { art, hash } = uncached[i]!
          setProgress({ current: i + 1, total: uncached.length })

          // Another run or the now-playing hook may have cached it since the scan above
          if (await segmentationCache.get(art, backendId)) continue

          // Resolve per-album override params or use global defaults
          const override = await maskOverrideStore.get(hash)
          const postParams: MaskPostProcessParams = override
            ? override.postProcessParams
            : { ...DEFAULT_MASK_PARAMS, ...maskDefaultsRef.current }
          const modelParams: MaskModelParams = override
            ? override.modelParams
            : { ...DEFAULT_MODEL_PARAMS }

          // If override specifies different model params, skip — withBackend loaded default model
          // The user's custom model config will be handled by useSegmentation on demand
          const resolution = modelParams.inputResolution || 256

          try {
            if (backend.estimateDepth) {
              const estimation = await backend.estimateDepth(art, resolution, resolution)
              if (estimation && !cancelled) {
                const result = await depthToMask(
                  estimation.depthMap, art, estimation.width, estimation.height, true, postParams,
                )
                if (result && !cancelled) {
                  await segmentationCache.put(art, backendId, result)
                }
              }
            } else {
              const result = await backend.segment(art, resolution, resolution)
              if (result && !cancelled) {
                await segmentationCache.put(art, backendId, result)
              }
            }
          } catch (err) {
            if (import.meta.env.DEV) console.warn('[batch-seg] Failed to process album art:', err)
          }
        }
      })

      if (!cancelled) {
        setProcessing(false)
        setProgress(null)
      }
    })().catch(() => {
      if (!cancelled) {
        setProcessing(false)
        setProgress(null)
      }
    })

    return () => {
      cancelled = true
      setProcessing(false)
      setProgress(null)
    }
  }, [enabled, backendId, artKey, runId])

  const processAll = useCallback(() => {
    if (!enabled || backendId === 'none' || backendId === 'manual' || (albums.length === 0 && tracks.length === 0)) return
    setRunId(n => n + 1)
  }, [enabled, backendId, albums.length, tracks.length])

  return { processing, progress, processAll }
}
