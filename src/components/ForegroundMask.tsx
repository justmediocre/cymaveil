import { useState, useEffect } from 'react'
import type React from 'react'
import type { SegmentationResult } from '../types'

interface ForegroundMaskProps {
  segmentation: SegmentationResult
  artSrc: string
  style?: React.CSSProperties
}

/**
 * Full-resolution foreground layer: renders the album art image masked by the
 * segmentation alpha so the visualizer shows through background regions.
 *
 * Uses CSS mask-image instead of a canvas so the browser rasterizes the
 * album art at native device resolution (HiDPI-sharp), while the 256px
 * segmentation mask is smoothly upscaled by the compositor.
 */
export default function ForegroundMask({ segmentation, artSrc, style }: ForegroundMaskProps) {
  const [maskUrl, setMaskUrl] = useState<string | null>(null)

  useEffect(() => {
    const { foregroundMask, width, height } = segmentation

    // Extract alpha channel from the segmentation into a standalone mask image
    const canvas = document.createElement('canvas')
    canvas.width = width
    canvas.height = height
    const ctx = canvas.getContext('2d')!
    const maskData = ctx.createImageData(width, height)
    const src = foregroundMask.data
    const dst = maskData.data
    for (let i = 0; i < src.length; i += 4) {
      dst[i] = 255            // R — white
      dst[i + 1] = 255        // G
      dst[i + 2] = 255        // B
      dst[i + 3] = src[i + 3]! // Alpha from segmentation
    }
    ctx.putImageData(maskData, 0, 0)
    setMaskUrl(canvas.toDataURL('image/png'))
  }, [segmentation])

  if (!maskUrl) return null

  return (
    <img
      data-foreground-mask
      src={artSrc}
      alt=""
      className="absolute inset-0 w-full h-full object-cover pointer-events-none rounded-2xl"
      style={{
        zIndex: 2,
        WebkitMaskImage: `url("${maskUrl}")`,
        maskImage: `url("${maskUrl}")`,
        WebkitMaskSize: '100% 100%',
        maskSize: '100% 100%',
        ...style,
      }}
      draggable={false}
    />
  )
}
