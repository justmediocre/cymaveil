import { useRef, useEffect, useCallback } from 'react'
import { motion } from 'motion/react'
import type { SegmentationBackend } from '../types'
import type { MaskBrushEditorState } from '../hooks/useMaskBrushEditor'
import { paintPoint, paintLine, commitStroke } from '../lib/brushEngine'
import { useTheme } from '../contexts/ThemeContext'

interface MaskBrushEditorProps {
  editor: MaskBrushEditorState
  backendId: SegmentationBackend
  onSave: () => void
  children?: React.ReactNode
}

export default function MaskBrushEditor({ editor, backendId, onSave, children }: MaskBrushEditorProps) {
  const {
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
    close,
  } = editor

  const { theme } = useTheme()
  const isDark = theme === 'dark'

  const artCanvasRef = useRef<HTMLCanvasElement>(null)
  const maskCanvasRef = useRef<HTMLCanvasElement>(null)
  const paintCanvasRef = useRef<HTMLCanvasElement>(null)
  const containerRef = useRef<HTMLDivElement>(null)
  const lastPointRef = useRef<{ x: number; y: number } | null>(null)
  const lastScreenPosRef = useRef<{ x: number; y: number } | null>(null)
  const isPaintingRef = useRef(false)
  const rafRef = useRef(0)

  // Screen-to-mask coordinate transform
  const screenToMask = useCallback((clientX: number, clientY: number): { x: number; y: number } | null => {
    const canvas = paintCanvasRef.current
    const state = brushStateRef.current
    if (!canvas || !state) return null
    const rect = canvas.getBoundingClientRect()
    return {
      x: (clientX - rect.left) / rect.width * state.width,
      y: (clientY - rect.top) / rect.height * state.height,
    }
  }, [brushStateRef])

  // Draw art on the base canvas
  useEffect(() => {
    if (!lockedArtSrc) return
    const canvas = artCanvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    const img = new Image()
    img.crossOrigin = 'anonymous'
    img.onload = () => {
      canvas.width = img.naturalWidth
      canvas.height = img.naturalHeight
      ctx.drawImage(img, 0, 0)
    }
    img.src = lockedArtSrc
  }, [lockedArtSrc])

  // Render mask overlay whenever previewVersion changes
  useEffect(() => {
    const state = brushStateRef.current
    const canvas = maskCanvasRef.current
    if (!state || !canvas) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    canvas.width = state.width
    canvas.height = state.height

    const imgData = ctx.createImageData(state.width, state.height)
    const d = imgData.data
    const { alpha, artRGBA, width, height } = state

    for (let i = 0; i < width * height; i++) {
      const a = alpha[i]!
      if (a > 128) {
        // Foreground: fully opaque — covers visualizer bars (subject "in front")
        d[i * 4] = artRGBA[i * 4]!
        d[i * 4 + 1] = artRGBA[i * 4 + 1]!
        d[i * 4 + 2] = artRGBA[i * 4 + 2]!
        d[i * 4 + 3] = 255
      } else {
        // Background: dim + transparent — visualizer bars show through
        const r = artRGBA[i * 4]!
        const g = artRGBA[i * 4 + 1]!
        const b = artRGBA[i * 4 + 2]!
        d[i * 4] = Math.round(r * 0.3)
        d[i * 4 + 1] = Math.round(g * 0.25)
        d[i * 4 + 2] = Math.round(b * 0.4)
        d[i * 4 + 3] = 90
      }
    }

    ctx.putImageData(imgData, 0, 0)
  }, [previewVersion, brushStateRef])

  // Draw brush cursor on paint canvas in screen-space (crisp at any zoom)
  const drawCursor = useCallback((screenX: number, screenY: number) => {
    const canvas = paintCanvasRef.current
    const state = brushStateRef.current
    if (!canvas || !state) return
    const rect = canvas.getBoundingClientRect()
    if (rect.width === 0 || rect.height === 0) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    const dpr = window.devicePixelRatio || 1
    const cw = Math.round(rect.width * dpr)
    const ch = Math.round(rect.height * dpr)
    if (canvas.width !== cw || canvas.height !== ch) {
      canvas.width = cw
      canvas.height = ch
    }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    ctx.clearRect(0, 0, rect.width, rect.height)

    // Convert mask-space brush radius to screen-space pixels
    const scale = rect.width / state.width
    const screenRadius = state.brushRadius * scale
    // Screen-relative position within the canvas
    const sx = screenX - rect.left
    const sy = screenY - rect.top

    const cursorColor = isDark ? 'rgba(255,255,255,0.8)' : 'rgba(0,0,0,0.7)'
    const crosshairColor = isDark ? 'rgba(255,255,255,0.5)' : 'rgba(0,0,0,0.4)'

    ctx.beginPath()
    ctx.arc(sx, sy, screenRadius, 0, Math.PI * 2)
    ctx.strokeStyle = state.mode === 'paint' ? cursorColor : 'rgba(255,80,80,0.8)'
    ctx.lineWidth = 1.5
    ctx.stroke()

    // Crosshair
    ctx.beginPath()
    ctx.moveTo(sx - 3, sy)
    ctx.lineTo(sx + 3, sy)
    ctx.moveTo(sx, sy - 3)
    ctx.lineTo(sx, sy + 3)
    ctx.strokeStyle = crosshairColor
    ctx.lineWidth = 1
    ctx.stroke()

    lastScreenPosRef.current = { x: screenX, y: screenY }
  }, [brushStateRef, isDark])

  // Redraw cursor when brush radius or mode changes (keyboard/scroll)
  useEffect(() => {
    const pos = lastScreenPosRef.current
    if (pos) drawCursor(pos.x, pos.y)
  }, [brushRadius, brushMode, drawCursor])

  // Schedule mask canvas redraw via rAF
  const scheduleMaskRedraw = useCallback(() => {
    if (rafRef.current) return
    rafRef.current = requestAnimationFrame(() => {
      rafRef.current = 0
      incrementPreview()
    })
  }, [incrementPreview])

  // Pointer events
  const handlePointerDown = useCallback((e: React.PointerEvent) => {
    const pt = screenToMask(e.clientX, e.clientY)
    if (!pt || !brushStateRef.current) return

    isPaintingRef.current = true
    lastPointRef.current = pt
    ;(e.target as HTMLElement).setPointerCapture(e.pointerId)

    paintPoint(brushStateRef.current, pt.x, pt.y)
    scheduleMaskRedraw()
  }, [screenToMask, brushStateRef, scheduleMaskRedraw])

  const handlePointerMove = useCallback((e: React.PointerEvent) => {
    const pt = screenToMask(e.clientX, e.clientY)
    if (!pt) return

    drawCursor(e.clientX, e.clientY)

    if (!isPaintingRef.current || !brushStateRef.current || !lastPointRef.current) return

    paintLine(brushStateRef.current, lastPointRef.current.x, lastPointRef.current.y, pt.x, pt.y)
    lastPointRef.current = pt
    scheduleMaskRedraw()
  }, [screenToMask, brushStateRef, drawCursor, scheduleMaskRedraw])

  const handlePointerUp = useCallback(() => {
    if (!isPaintingRef.current || !brushStateRef.current) return
    isPaintingRef.current = false
    lastPointRef.current = null
    commitStroke(brushStateRef.current)
    incrementPreview()
    syncUndoRedo()
  }, [brushStateRef, incrementPreview, syncUndoRedo])

  // Ctrl+Mousewheel for brush size
  useEffect(() => {
    const container = containerRef.current
    if (!container) return

    const handler = (e: WheelEvent) => {
      if (!e.ctrlKey) return
      e.preventDefault()
      const delta = e.deltaY > 0 ? -1 : 1
      setBrushRadius(brushRadius + delta)
    }
    container.addEventListener('wheel', handler, { passive: false })
    return () => container.removeEventListener('wheel', handler)
  }, [brushRadius, setBrushRadius])

  // Keyboard shortcuts
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      // Use capture phase and stop propagation to block global handlers
      if (e.key === 'Escape') {
        e.stopPropagation()
        e.preventDefault()
        close()
        return
      }

      if (e.ctrlKey && e.key === 'z') {
        e.stopPropagation()
        e.preventDefault()
        undo()
        return
      }

      if (e.ctrlKey && (e.key === 'y' || (e.shiftKey && e.key === 'Z'))) {
        e.stopPropagation()
        e.preventDefault()
        redo()
        return
      }

      if (e.ctrlKey && e.key === 's') {
        e.stopPropagation()
        e.preventDefault()
        handleSave()
        return
      }

      if (!e.ctrlKey && !e.altKey && !e.metaKey) {
        if (e.key === 'x' || e.key === 'X') {
          e.stopPropagation()
          e.preventDefault()
          toggleBrushMode()
          return
        }
        if (e.key === '[') {
          e.stopPropagation()
          e.preventDefault()
          setBrushRadius(brushRadius - 1)
          return
        }
        if (e.key === ']') {
          e.stopPropagation()
          e.preventDefault()
          setBrushRadius(brushRadius + 1)
          return
        }
      }
    }

    window.addEventListener('keydown', handler, true)
    return () => window.removeEventListener('keydown', handler, true)
  }, [close, undo, redo, toggleBrushMode, brushRadius, setBrushRadius])

  const handleSave = useCallback(async () => {
    await save(backendId)
    onSave()
    close()
  }, [save, backendId, onSave, close])

  // Cleanup rAF on unmount
  useEffect(() => {
    return () => {
      if (rafRef.current) cancelAnimationFrame(rafRef.current)
    }
  }, [])

  return (
    <motion.div
      initial={{ opacity: 0 }}
      animate={{ opacity: 1 }}
      exit={{ opacity: 0 }}
      transition={{ duration: 0.25 }}
      ref={containerRef}
      style={{
        position: 'fixed',
        inset: 0,
        zIndex: 100,
        background: 'var(--bg-primary)',
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        justifyContent: 'center',
        cursor: 'none',
        padding: '24px 24px 80px',
        gap: 16,
      }}
    >
      {/* Canvas stack — sized to fit above the toolbar */}
      <div
        style={{
          position: 'relative',
          width: `min(85vw, calc(100vh - 160px))`,
          height: `min(85vw, calc(100vh - 160px))`,
          flexShrink: 0,
        }}
      >
        {/* Art canvas — raw album art as visual reference */}
        <canvas
          ref={artCanvasRef}
          style={{
            position: 'absolute',
            inset: 0,
            width: '100%',
            height: '100%',
            imageRendering: 'auto',
            borderRadius: 12,
            zIndex: 0,
          }}
        />

        {/* Visualizer overlay (children slot) — renders between art and mask */}
        {children && (
          <div
            style={{
              position: 'absolute',
              inset: 0,
              borderRadius: 12,
              overflow: 'hidden',
              pointerEvents: 'none',
              zIndex: 1,
            }}
          >
            {children}
          </div>
        )}

        {/* Mask overlay canvas — z-index 2 so it sits above the visualizer (zIndex 1) */}
        <canvas
          ref={maskCanvasRef}
          style={{
            position: 'absolute',
            inset: 0,
            width: '100%',
            height: '100%',
            imageRendering: 'auto',
            borderRadius: 12,
            zIndex: 2,
          }}
        />

        {/* Paint canvas — captures pointer events, renders brush cursor */}
        <canvas
          ref={paintCanvasRef}
          style={{
            position: 'absolute',
            inset: 0,
            width: '100%',
            height: '100%',
            imageRendering: 'auto',
            borderRadius: 12,
            zIndex: 3,
          }}
          onPointerDown={handlePointerDown}
          onPointerMove={handlePointerMove}
          onPointerUp={handlePointerUp}
          onPointerLeave={() => {
            const canvas = paintCanvasRef.current
            if (canvas) {
              const ctx = canvas.getContext('2d')
              if (ctx) ctx.clearRect(0, 0, canvas.width, canvas.height)
            }
            lastScreenPosRef.current = null
          }}
        />
      </div>

      {/* Toolbar — glass panel, below canvas */}
      <div
        className="glass"
        style={{
          display: 'flex',
          alignItems: 'center',
          gap: 12,
          padding: '10px 20px',
          borderRadius: 16,
          background: 'var(--glass-bg-surface)',
          border: '1px solid var(--border-subtle)',
          fontFamily: 'var(--font-sans, system-ui, sans-serif)',
          fontSize: 12,
          flexShrink: 0,
        }}
      >
        {/* Paint/Erase toggle */}
        <div
          style={{
            display: 'flex',
            borderRadius: 8,
            overflow: 'hidden',
            border: '1px solid var(--border)',
          }}
        >
          <button
            onClick={() => setBrushMode('paint')}
            style={{
              padding: '6px 12px',
              fontSize: 11,
              background: brushMode === 'paint' ? 'var(--accent)' : 'var(--bg-elevated)',
              color: brushMode === 'paint' ? '#fff' : 'var(--text-secondary)',
              border: 'none',
              cursor: 'pointer',
              transition: 'background 0.15s, color 0.15s',
            }}
          >
            Paint
          </button>
          <button
            onClick={() => setBrushMode('erase')}
            style={{
              padding: '6px 12px',
              fontSize: 11,
              background: brushMode === 'erase' ? 'var(--accent)' : 'var(--bg-elevated)',
              color: brushMode === 'erase' ? '#fff' : 'var(--text-secondary)',
              border: 'none',
              cursor: 'pointer',
              transition: 'background 0.15s, color 0.15s',
            }}
          >
            Erase
          </button>
        </div>

        {/* Brush size display */}
        <div
          style={{
            display: 'flex',
            alignItems: 'center',
            gap: 6,
            color: 'var(--text-secondary)',
          }}
        >
          <span style={{ fontSize: 10, color: 'var(--text-tertiary)' }}>Size</span>
          <span
            style={{
              fontFamily: 'var(--font-mono)',
              fontSize: 11,
              color: 'var(--text-primary)',
              minWidth: 20,
              textAlign: 'center',
            }}
          >
            {brushRadius}
          </span>
        </div>

        {/* Divider */}
        <div style={{ width: 1, height: 20, background: 'var(--border-subtle)' }} />

        {/* Undo */}
        <button
          onClick={undo}
          disabled={!canUndo}
          title="Undo (Ctrl+Z)"
          style={{
            padding: '4px 8px',
            background: 'none',
            border: 'none',
            color: canUndo ? 'var(--text-secondary)' : 'var(--text-tertiary)',
            cursor: canUndo ? 'pointer' : 'default',
            opacity: canUndo ? 1 : 0.4,
            display: 'flex',
            alignItems: 'center',
          }}
        >
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
            <polyline points="1 4 1 10 7 10" />
            <path d="M3.51 15a9 9 0 1 0 2.13-9.36L1 10" />
          </svg>
        </button>

        {/* Redo */}
        <button
          onClick={redo}
          disabled={!canRedo}
          title="Redo (Ctrl+Y)"
          style={{
            padding: '4px 8px',
            background: 'none',
            border: 'none',
            color: canRedo ? 'var(--text-secondary)' : 'var(--text-tertiary)',
            cursor: canRedo ? 'pointer' : 'default',
            opacity: canRedo ? 1 : 0.4,
            display: 'flex',
            alignItems: 'center',
          }}
        >
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
            <polyline points="23 4 23 10 17 10" />
            <path d="M20.49 15a9 9 0 1 1-2.13-9.36L23 10" />
          </svg>
        </button>

        {/* Divider */}
        <div style={{ width: 1, height: 20, background: 'var(--border-subtle)' }} />

        {/* Save button */}
        <button
          onClick={handleSave}
          disabled={saving}
          title="Save (Ctrl+S)"
          style={{
            padding: '6px 16px',
            fontSize: 11,
            fontWeight: 500,
            background: 'var(--accent)',
            color: '#fff',
            border: 'none',
            borderRadius: 8,
            cursor: saving ? 'default' : 'pointer',
            opacity: saving ? 0.6 : 1,
            transition: 'opacity 0.15s',
          }}
        >
          {saving ? 'Saving...' : 'Save'}
        </button>

        {/* Close button */}
        <button
          onClick={close}
          title="Close (Escape)"
          style={{
            padding: '4px 8px',
            background: 'none',
            border: 'none',
            color: 'var(--text-tertiary)',
            cursor: 'pointer',
            display: 'flex',
            alignItems: 'center',
          }}
        >
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
            <line x1="18" y1="6" x2="6" y2="18" />
            <line x1="6" y1="6" x2="18" y2="18" />
          </svg>
        </button>
      </div>

      {/* Keyboard hints — top-right */}
      <div
        style={{
          position: 'absolute',
          top: 16,
          right: 16,
          display: 'flex',
          flexDirection: 'column',
          gap: 4,
          fontFamily: 'var(--font-mono)',
          fontSize: 10,
          color: 'var(--text-tertiary)',
          opacity: 0.6,
          pointerEvents: 'none',
          textAlign: 'right',
        }}
      >
        <span>X — toggle paint/erase</span>
        <span>[ ] — brush size</span>
        <span>Ctrl+Scroll — brush size</span>
        <span>Ctrl+Z / Ctrl+Y — undo/redo</span>
        <span>Ctrl+S — save</span>
        <span>Esc — close</span>
      </div>
    </motion.div>
  )
}
