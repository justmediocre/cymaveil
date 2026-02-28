import { useState, useCallback, useRef, useEffect } from 'react'
import type { SegmentationResult, MaskPostProcessParams, MaskModelParams } from '../types'
import { depthToMask, DEFAULT_MASK_PARAMS } from '../lib/segmentation/depthToMask'
import { segmentationCache, hashArtSrc } from '../lib/segmentation/cache'
import { maskOverrideStore } from '../lib/segmentation/maskOverrideStore'
import { withBackend } from '../lib/segmentation/registry'
import type { SegmentationBackend } from '../types'

export interface MaskEditorState {
  isOpen: boolean
  open: () => void
  close: () => void
  toggle: () => void
  previewSegmentation: SegmentationResult | null
  reprocessing: boolean
  downloadProgress: number | null
  previewFromParams: (
    depthMap: Uint8Array,
    artSrc: string,
    width: number,
    height: number,
    params: MaskPostProcessParams,
  ) => void
  reprocess: (
    artSrc: string,
    backendId: SegmentationBackend,
    modelParams: MaskModelParams,
    postProcessParams: MaskPostProcessParams,
  ) => Promise<void>
  save: (
    artSrc: string,
    backendId: SegmentationBackend,
    modelParams: MaskModelParams,
    postProcessParams: MaskPostProcessParams,
  ) => Promise<void>
  removeOverride: (
    artSrc: string,
    backendId: SegmentationBackend,
    defaultPostParams: MaskPostProcessParams,
  ) => Promise<void>
  clearPreview: () => void
}

export default function useMaskEditor(artSrc: string | null): MaskEditorState {
  const [isOpen, setIsOpen] = useState(false)
  const [previewSegmentation, setPreviewSegmentation] = useState<SegmentationResult | null>(null)
  const [reprocessing, setReprocessing] = useState(false)
  const [downloadProgress, setDownloadProgress] = useState<number | null>(null)
  const latestPreviewRef = useRef(0)
  const previewRef = useRef<SegmentationResult | null>(null)
  previewRef.current = previewSegmentation
  const prevArtRef = useRef(artSrc)

  // Clear stale preview and invalidate in-flight previews when art changes
  useEffect(() => {
    if (artSrc !== prevArtRef.current) {
      prevArtRef.current = artSrc
      setPreviewSegmentation(null)
      latestPreviewRef.current++ // invalidate any pending preview
    }
  }, [artSrc])

  const open = useCallback(() => setIsOpen(true), [])
  const close = useCallback(() => {
    setIsOpen(false)
    setPreviewSegmentation(null)
  }, [])
  const toggle = useCallback(() => setIsOpen(prev => !prev), [])
  const clearPreview = useCallback(() => setPreviewSegmentation(null), [])

  const previewFromParams = useCallback((
    depthMap: Uint8Array,
    artSrc: string,
    width: number,
    height: number,
    params: MaskPostProcessParams,
  ) => {
    const id = ++latestPreviewRef.current
    depthToMask(depthMap, artSrc, width, height, true, params).then(result => {
      // Only apply if this is still the latest preview request
      if (latestPreviewRef.current === id) {
        setPreviewSegmentation(result)
      }
    }).catch(() => {
      // Preview failure is non-fatal
    })
  }, [])

  const reprocess = useCallback(async (
    artSrc: string,
    backendId: SegmentationBackend,
    modelParams: MaskModelParams,
    postProcessParams: MaskPostProcessParams,
  ) => {
    setReprocessing(true)
    setDownloadProgress(null)
    try {
      const resolution = modelParams.inputResolution || 256
      const onProgress = (p: number) => setDownloadProgress(p)
      await withBackend(backendId, modelParams, onProgress, async (backend) => {
        setDownloadProgress(null)

        if (backend.estimateDepth) {
          const estimation = await backend.estimateDepth(artSrc, resolution, resolution)
          if (estimation) {
            const result = await depthToMask(
              estimation.depthMap, artSrc, estimation.width, estimation.height, true, postProcessParams,
            )
            setPreviewSegmentation(result)
          }
        } else {
          const result = await backend.segment(artSrc, resolution, resolution)
          if (result) {
            setPreviewSegmentation(result)
          }
        }
      })
    } catch (err) {
      if (import.meta.env.DEV) console.error('[mask-editor] Reprocess failed:', err)
    } finally {
      setReprocessing(false)
      setDownloadProgress(null)
    }
  }, [])

  const save = useCallback(async (
    artSrc: string,
    backendId: SegmentationBackend,
    modelParams: MaskModelParams,
    postProcessParams: MaskPostProcessParams,
  ) => {
    const hash = await hashArtSrc(artSrc)

    // Save override params
    await maskOverrideStore.put(hash, modelParams, postProcessParams)

    // Save the preview result to cache as user-edited
    const current = previewRef.current
    if (current) {
      await segmentationCache.putUserEdited(artSrc, backendId, current)
    }
  }, [])

  const removeOverride = useCallback(async (
    artSrc: string,
    backendId: SegmentationBackend,
    defaultPostParams: MaskPostProcessParams,
  ) => {
    const hash = await hashArtSrc(artSrc)
    await maskOverrideStore.remove(hash)

    // Re-process with defaults from cached depth map
    const depthData = await segmentationCache.getDepthMap(artSrc, backendId)
    if (depthData) {
      const result = await depthToMask(
        depthData.depthMap, artSrc, depthData.width, depthData.height, true, defaultPostParams,
      )
      setPreviewSegmentation(result)
      await segmentationCache.put(artSrc, backendId, result)
    }
  }, [])

  return {
    isOpen, open, close, toggle,
    previewSegmentation, reprocessing, downloadProgress,
    previewFromParams, reprocess, save, removeOverride, clearPreview,
  }
}
