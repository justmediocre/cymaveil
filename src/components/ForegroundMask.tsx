import { useRef, useEffect } from 'react'
import type React from 'react'
import type { SegmentationResult } from '../types'

interface ForegroundMaskProps {
  segmentation: SegmentationResult
  style?: React.CSSProperties
}

/**
 * Static canvas that draws foreground pixels of album art over the visualizer.
 * Background pixels are transparent so the visualizer shows through.
 * Re-renders only when the segmentation result changes.
 */
export default function ForegroundMask({ segmentation, style }: ForegroundMaskProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const tmpCanvasRef = useRef<OffscreenCanvas | null>(null)

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return

    const ctx = canvas.getContext('2d')
    if (!ctx) return

    // Reuse OffscreenCanvas across resize events; only reallocate if mask dimensions change
    if (
      !tmpCanvasRef.current ||
      tmpCanvasRef.current.width !== segmentation.width ||
      tmpCanvasRef.current.height !== segmentation.height
    ) {
      tmpCanvasRef.current = new OffscreenCanvas(segmentation.width, segmentation.height)
    }
    const tmp = tmpCanvasRef.current
    const tmpCtx = tmp.getContext('2d')!
    tmpCtx.putImageData(segmentation.foregroundMask, 0, 0)

    let resizeRafId: number | null = null

    const resize = () => {
      const dpr = window.devicePixelRatio || 1
      const rect = canvas.getBoundingClientRect()
      const w = rect.width
      const h = rect.height
      if (w === 0 || h === 0) return

      canvas.width = w * dpr
      canvas.height = h * dpr
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)

      // Draw scaled to display
      ctx.clearRect(0, 0, w, h)
      ctx.drawImage(tmp, 0, 0, w, h)
    }

    // Coalesce rapid ResizeObserver callbacks during window drag-resize
    const debouncedResize = () => {
      if (resizeRafId !== null) return
      resizeRafId = requestAnimationFrame(() => {
        resizeRafId = null
        resize()
      })
    }

    const observer = new ResizeObserver(debouncedResize)
    observer.observe(canvas)
    resize()

    return () => {
      observer.disconnect()
      if (resizeRafId !== null) cancelAnimationFrame(resizeRafId)
    }
  }, [segmentation])

  return (
    <canvas
      ref={canvasRef}
      className="absolute inset-0 w-full h-full pointer-events-none rounded-2xl"
      style={{ zIndex: 2, ...style }}
    />
  )
}
