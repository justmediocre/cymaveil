import { useState, useEffect, useRef, useCallback } from 'react'
import type { SegmentationResult, MaskPostProcessParams, MaskModelParams } from '../types'
import { segmentationCache, hashArtSrc } from '../lib/segmentation/cache'
import { maskOverrideStore } from '../lib/segmentation/maskOverrideStore'
import { withBackend } from '../lib/segmentation/registry'
import { DEFAULT_MASK_PARAMS } from '../lib/segmentation/depthToMask'
import useVisualSettings from './useVisualSettings'

const SEGMENT_SIZE = 256

const DEFAULT_MODEL_PARAMS: MaskModelParams = {
  modelSize: 'small',
  modelDtype: 'q8',
  inputResolution: 256,
}

export { DEFAULT_MODEL_PARAMS }

export interface SegmentationState {
  segmentation: SegmentationResult | null
  loading: boolean
  depthMap: Uint8Array | null
  effectivePostProcessParams: MaskPostProcessParams
  effectiveModelParams: MaskModelParams
  hasOverride: boolean
  /** Whether the cached mask has user brush paint edits */
  hasUserPaint: boolean
  artHash: string | null
  /** Force re-evaluation (e.g. after saving/removing an override) */
  refresh: () => void
}

export default function useSegmentation(artSrc: string | null): SegmentationState {
  const { settings } = useVisualSettings()
  const [segmentation, setSegmentation] = useState<SegmentationResult | null>(null)
  const [loading, setLoading] = useState(false)
  const [depthMap, setDepthMap] = useState<Uint8Array | null>(null)
  const [effectivePostProcessParams, setEffectivePostProcessParams] = useState<MaskPostProcessParams>(DEFAULT_MASK_PARAMS)
  const [effectiveModelParams, setEffectiveModelParams] = useState<MaskModelParams>(DEFAULT_MODEL_PARAMS)
  const [hasOverride, setHasOverride] = useState(false)
  const [hasUserPaint, setHasUserPaint] = useState(false)
  const [artHash, setArtHash] = useState<string | null>(null)
  const [refreshKey, setRefreshKey] = useState(0)
  const prevKeyRef = useRef<string | null>(null)

  const backendId = settings.depthLayerEnabled ? settings.segmentationBackend : 'none'

  const refresh = useCallback(() => {
    prevKeyRef.current = null
    setRefreshKey(k => k + 1)
  }, [])

  useEffect(() => {
    const cacheVer = settings.maskCacheVersion ?? 0
    const key = `${backendId}:${artSrc}:${cacheVer}`
    if (key === prevKeyRef.current) return
    prevKeyRef.current = key

    if (!artSrc || backendId === 'none' || artSrc.startsWith('data:image/svg+xml')) {
      setSegmentation(null)
      setLoading(false)
      setDepthMap(null)
      setHasOverride(false)
      setHasUserPaint(false)
      setArtHash(null)
      return
    }

    let cancelled = false
    setSegmentation(null)
    setLoading(true)
    setDepthMap(null)

    ;(async () => {
      // Compute art hash for override lookup
      const hash = await hashArtSrc(artSrc)
      if (cancelled) return
      setArtHash(hash)

      // Resolve effective params: per-album override > global defaults > built-in defaults
      const override = await maskOverrideStore.get(hash)
      if (cancelled) return

      const postParams: MaskPostProcessParams = override
        ? override.postProcessParams
        : { ...DEFAULT_MASK_PARAMS, ...settings.maskDefaults }

      const modelParams: MaskModelParams = override
        ? override.modelParams
        : { ...DEFAULT_MODEL_PARAMS }

      setEffectivePostProcessParams(postParams)
      setEffectiveModelParams(modelParams)

      // Check cache first (works for both ML-generated and user-painted masks)
      const cached = await segmentationCache.get(artSrc, backendId)
      if (cancelled) return

      // Always check if the cached mask is user-edited (brush painted)
      const userEdited = cached
        ? await segmentationCache.isUserEdited(artSrc, backendId)
        : false
      if (cancelled) return
      setHasUserPaint(userEdited)
      // hasOverride: true if there's a parameter override OR a user-painted mask in cache
      setHasOverride(!!override || userEdited)

      if (cached) {
        setSegmentation(cached)
        setDepthMap(cached.depthMap)
        setLoading(false)
        return
      }

      // Manual backend: no ML model to run — just return null if no cached mask
      if (backendId === 'manual') {
        setSegmentation(null)
        setLoading(false)
        return
      }

      // Re-check cache before acquiring the backend lock — batch segmentation
      // may have populated it while we were waiting in the queue
      const rechecked = await segmentationCache.get(artSrc, backendId)
      if (cancelled) return
      if (rechecked) {
        setSegmentation(rechecked)
        setDepthMap(rechecked.depthMap)
        setLoading(false)
        setHasOverride(!!override || await segmentationCache.isUserEdited(artSrc, backendId))
        return
      }

      // Foreground priority: jumps ahead of any queued batch covers
      const resolution = modelParams.inputResolution || SEGMENT_SIZE
      const result = await withBackend(backendId, modelParams, null, async (backend) => {
        if (cancelled) return null

        // Final cache check after acquiring lock — avoids redundant model run
        // when batch segmentation cached the result while we waited on the mutex
        const postLockCached = await segmentationCache.get(artSrc, backendId)
        if (postLockCached) return postLockCached

        const seg = await backend.segment(artSrc, resolution, resolution, postParams)
        // Store before releasing the lock so a queued batch job sees it, and keep it
        // even if the track changed meanwhile — the work is done either way
        if (seg) await segmentationCache.put(artSrc, backendId, seg)
        if (cancelled) return null
        if (seg) setDepthMap(seg.depthMap)
        return seg
      })
      if (cancelled) return

      setSegmentation(result)
      setLoading(false)
    })().catch((err: unknown) => {
      if (!cancelled) {
        const msg = err instanceof Error ? err.message : String(err)
        console.warn('[segmentation] failed:', msg)
        setSegmentation(null)
        setLoading(false)
        setDepthMap(null)
        setHasUserPaint(false)
      }
    })

    return () => {
      cancelled = true
      // Reset so React strict mode's remount re-runs the effect instead of
      // hitting the prevKeyRef dedup (which would leave loading stuck at true)
      prevKeyRef.current = null
    }
  }, [artSrc, backendId, settings.maskDefaults, settings.maskCacheVersion, refreshKey])

  return { segmentation, loading, depthMap, effectivePostProcessParams, effectiveModelParams, hasOverride, hasUserPaint, artHash, refresh }
}
