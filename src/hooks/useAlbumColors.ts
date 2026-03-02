import { useState, useEffect } from 'react'
import type { Album, AlbumColors } from '../types'
import { extractColors } from '../lib/colorExtractor'
import { artCache } from '../lib/artCache'

/**
 * Extract dominant/accent colors from album art.
 * Falls back to album's predefined colors if extraction fails or art is SVG.
 * Results are cached to IndexedDB for instant retrieval on repeat visits.
 */
export default function useAlbumColors(album: Album | null): AlbumColors {
  const [colors, setColors] = useState<AlbumColors>({
    dominant: album?.dominantColor || '#1a1a2e',
    accent: album?.accentColor || '#4a90d9',
  })

  useEffect(() => {
    if (!album) return

    // If art is a base64 image (not SVG), try to extract real colors
    if (album.art && !album.art.startsWith('data:image/svg+xml')) {
      let cancelled = false
      const artSrc = album.art

      ;(async () => {
        // Check cache first
        const cached = await artCache.getColors(artSrc)
        if (cached && !cancelled) {
          setColors(cached)
          return
        }

        // Extract and cache
        const extracted = await extractColors(artSrc)
        if (cancelled) return
        if (extracted) {
          const result: AlbumColors = {
            dominant: extracted.dominant,
            accent: extracted.accent,
            accentSecondary: extracted.accentSecondary,
          }
          setColors(result)
          artCache.putColors(artSrc, result)
        } else {
          setColors({ dominant: album.dominantColor, accent: album.accentColor })
        }
      })()

      return () => { cancelled = true }
    } else {
      setColors({ dominant: album.dominantColor, accent: album.accentColor })
    }
  }, [album?.id, album?.art])

  return colors
}
