import { useState, useCallback, useRef } from 'react'
import type { SegmentationResult, SegmentationBackend } from '../types'
import { segmentationCache } from '../lib/segmentation/cache'
import {
  createBrushState,
  createBlankBrushState,
  composeMask,
  undo as brushUndo,
  redo as brushRedo,
  saveInitialState,
  type BrushState,
} from '../lib/brushEngine'

export interface MaskBrushEditorState {
  isOpen: boolean
  open: (segmentation: SegmentationResult | null, artSrc: string) => void
  close: () => void
  lockedArtSrc: string | null
  brushStateRef: React.RefObject<BrushState | null>
  previewVersion: number
  incrementPreview: () => void
  brushRadius: number
  setBrushRadius: (r: number) => void
  brushMode: 'paint' | 'erase'
  setBrushMode: (m: 'paint' | 'erase') => void
  toggleBrushMode: () => void
  canUndo: boolean
  canRedo: boolean
  undo: () => void
  redo: () => void
  syncUndoRedo: () => void
  save: (backendId: SegmentationBackend) => Promise<void>
  saving: boolean
}

const MASK_SIZE = 256

export default function useMaskBrushEditor(): MaskBrushEditorState {
  const [isOpen, setIsOpen] = useState(false)
  const [lockedArtSrc, setLockedArtSrc] = useState<string | null>(null)
  const [previewVersion, setPreviewVersion] = useState(0)
  const [brushRadius, setBrushRadiusState] = useState(12)
  const [brushMode, setBrushModeState] = useState<'paint' | 'erase'>('paint')
  const [canUndo, setCanUndo] = useState(false)
  const [canRedo, setCanRedo] = useState(false)
  const [saving, setSaving] = useState(false)
  const brushStateRef = useRef<BrushState | null>(null)

  const syncUndoRedo = useCallback(() => {
    const s = brushStateRef.current
    if (s) {
      setCanUndo(s.undoStack.length > 0)
      setCanRedo(s.redoStack.length > 0)
    }
  }, [])

  const incrementPreview = useCallback(() => {
    setPreviewVersion(v => v + 1)
  }, [])

  const open = useCallback(async (segmentation: SegmentationResult | null, artSrc: string) => {
    setLockedArtSrc(artSrc)
    setBrushModeState('paint')
    setCanUndo(false)
    setCanRedo(false)

    let state: BrushState
    if (segmentation) {
      state = await createBrushState(segmentation, artSrc)
    } else {
      state = await createBlankBrushState(artSrc, MASK_SIZE, MASK_SIZE, 255)
    }
    saveInitialState(state)
    setBrushRadiusState(state.brushRadius)
    brushStateRef.current = state
    setCanUndo(state.undoStack.length > 0)
    setIsOpen(true)
    setPreviewVersion(0)
  }, [])

  const close = useCallback(() => {
    setIsOpen(false)
    brushStateRef.current = null
    setLockedArtSrc(null)
  }, [])

  const setBrushRadius = useCallback((r: number) => {
    const clamped = Math.max(1, Math.min(64, r))
    setBrushRadiusState(clamped)
    if (brushStateRef.current) {
      brushStateRef.current.brushRadius = clamped
    }
  }, [])

  const setBrushMode = useCallback((m: 'paint' | 'erase') => {
    setBrushModeState(m)
    if (brushStateRef.current) {
      brushStateRef.current.mode = m
    }
  }, [])

  const toggleBrushMode = useCallback(() => {
    setBrushModeState(prev => {
      const next = prev === 'paint' ? 'erase' : 'paint'
      if (brushStateRef.current) {
        brushStateRef.current.mode = next
      }
      return next
    })
  }, [])

  const undo = useCallback(() => {
    if (brushStateRef.current && brushUndo(brushStateRef.current)) {
      incrementPreview()
      syncUndoRedo()
    }
  }, [incrementPreview, syncUndoRedo])

  const redo = useCallback(() => {
    if (brushStateRef.current && brushRedo(brushStateRef.current)) {
      incrementPreview()
      syncUndoRedo()
    }
  }, [incrementPreview, syncUndoRedo])

  const save = useCallback(async (backendId: SegmentationBackend) => {
    const state = brushStateRef.current
    const artSrc = lockedArtSrc
    if (!state || !artSrc) return

    setSaving(true)
    try {
      const result = composeMask(state)
      await segmentationCache.putUserEdited(artSrc, backendId, result)
    } finally {
      setSaving(false)
    }
  }, [lockedArtSrc])

  return {
    isOpen,
    open,
    close,
    lockedArtSrc,
    brushStateRef,
    previewVersion,
    incrementPreview,
    brushRadius,
    setBrushRadius,
    brushMode,
    setBrushMode,
    toggleBrushMode,
    canUndo,
    canRedo,
    undo,
    redo,
    syncUndoRedo,
    save,
    saving,
  }
}
