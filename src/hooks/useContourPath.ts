import { useState, useEffect, useRef } from 'react'
import type { ContourData } from '../types'
import { detectEdges } from '../lib/edgeDetector'
import { extractContour } from '../lib/contourPath'
import { artCache } from '../lib/artCache'

/**
 * Hook that runs edge detection + contour extraction when album art changes.
 * Returns contours in normalized 0-1 space with per-contour extent values.
 * Results are cached to IndexedDB for instant retrieval on repeat visits.
 */
export default function useContourPath(artSrc: string | null) {
  const [contourData, setContourData] = useState<ContourData | null>(null)
  const prevSrcRef = useRef<string | null>(null)

  useEffect(() => {
    if (artSrc === prevSrcRef.current) return
    prevSrcRef.current = artSrc

    if (!artSrc || artSrc.startsWith('data:image/svg+xml')) {
      setContourData(extractContour(null))
      return
    }

    let cancelled = false

    ;(async () => {
      // Check cache first
      const cached = await artCache.getContour(artSrc)
      if (cached && !cancelled) {
        setContourData(cached)
        return
      }

      // Compute and cache
      const edgeResult = await detectEdges(artSrc)
      if (cancelled) return
      const result = extractContour(edgeResult)
      setContourData(result)
      artCache.putContour(artSrc, result)
    })()

    return () => {
      cancelled = true
    }
  }, [artSrc])

  return { contourData }
}
