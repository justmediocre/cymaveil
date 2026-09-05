import { useState, useEffect, useRef, useCallback } from 'react'
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
  const cancelledRef = useRef(false)
  const [runId, setRunId] = useState(0)

  const enabled = settings.depthLayerEnabled
  const backendId = settings.segmentationBackend

  useEffect(() => {
    if (!enabled || backendId === 'none' || backendId === 'manual') return

    cancelledRef.current = false

    // Unique art sources: album covers plus per-track overrides
    const artSources: string[] = []
    const seen = new Set<string>()
    for (const src of [...albums.map(a => a.art), ...tracks.map(t => t.art ?? null)]) {
      if (src && !src.startsWith('data:image/svg+xml') && !seen.has(src)) {
        seen.add(src)
        artSources.push(src)
      }
    }
    if (artSources.length === 0) return

    let active = true

    ;(async () => {
      // Find uncached art sources
      const uncached: { art: string; hash: string }[] = []
      for (const art of artSources) {
        if (cancelledRef.current) return
        const cached = await segmentationCache.get(art, backendId)
        if (!cached) {
          const hash = await hashArtSrc(art)
          uncached.push({ art, hash })
        }
      }

      if (uncached.length === 0 || cancelledRef.current) return

      setProcessing(true)
      setProgress({ current: 0, total: uncached.length })

      await withBackend(backendId, DEFAULT_MODEL_PARAMS, null, async (backend) => {
        for (let i = 0; i < uncached.length; i++) {
          if (cancelledRef.current) return

          const { art, hash } = uncached[i]!
          setProgress({ current: i + 1, total: uncached.length })

          // Resolve per-album override params or use global defaults
          const override = await maskOverrideStore.get(hash)
          const postParams: MaskPostProcessParams = override
            ? override.postProcessParams
            : { ...DEFAULT_MASK_PARAMS, ...settings.maskDefaults }
          const modelParams: MaskModelParams = override
            ? override.modelParams
            : { ...DEFAULT_MODEL_PARAMS }

          // If override specifies different model params, skip — withBackend loaded default model
          // The user's custom model config will be handled by useSegmentation on demand
          const resolution = modelParams.inputResolution || 256

          try {
            if (backend.estimateDepth) {
              const estimation = await backend.estimateDepth(art, resolution, resolution)
              if (estimation && !cancelledRef.current) {
                const result = await depthToMask(
                  estimation.depthMap, art, estimation.width, estimation.height, true, postParams,
                )
                if (result && !cancelledRef.current) {
                  await segmentationCache.put(art, backendId, result)
                }
              }
            } else {
              const result = await backend.segment(art, resolution, resolution)
              if (result && !cancelledRef.current) {
                await segmentationCache.put(art, backendId, result)
              }
            }
          } catch (err) {
            if (import.meta.env.DEV) console.warn('[batch-seg] Failed to process album art:', err)
          }
        }
      })

      if (active) {
        setProcessing(false)
        setProgress(null)
      }
    })().catch(() => {
      if (active) {
        setProcessing(false)
        setProgress(null)
      }
    })

    return () => {
      active = false
      cancelledRef.current = true
      setProcessing(false)
      setProgress(null)
    }
  }, [enabled, backendId, albums, tracks, settings.maskDefaults, runId])

  const processAll = useCallback(() => {
    if (!enabled || backendId === 'none' || backendId === 'manual' || (albums.length === 0 && tracks.length === 0)) return
    setRunId(n => n + 1)
  }, [enabled, backendId, albums.length, tracks.length])

  return { processing, progress, processAll }
}
