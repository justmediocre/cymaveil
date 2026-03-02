import { memo, useState, useRef, useEffect } from 'react'
import type React from 'react'
import { motion, AnimatePresence } from 'motion/react'
import { onTick } from '../lib/tickLoop'
import useVisualSettings from '../hooks/useVisualSettings'
import { useTheme } from '../contexts/ThemeContext'
import ForegroundMask from './ForegroundMask'
import type { Album, SegmentationResult } from '../types'

interface AlbumArtProps {
  album: Album
  isPlaying: boolean
  trackIndex: number
  bassEnergyRef: React.RefObject<number>
  children?: React.ReactNode
  segmentation?: SegmentationResult | null
  segmentationLoading?: boolean
  transitionIntent?: 'prefire' | 'skip' | null
  onTransitionDone?: () => void
  onEditMask?: () => void
  onEditBrush?: () => void
  onDisplayedAlbumChange?: (album: Album) => void
  hasOverride?: boolean
  depthLayerActive?: boolean
}

export default memo(function AlbumArt({ album, isPlaying, trackIndex, bassEnergyRef, children, segmentation, segmentationLoading, transitionIntent, onTransitionDone, onEditMask, onEditBrush, onDisplayedAlbumChange, hasOverride, depthLayerActive }: AlbumArtProps) {
  const { isLight } = useTheme()
  const { settings } = useVisualSettings()
  const [isHovered, setIsHovered] = useState(false)
  const shakeRef = useRef<HTMLDivElement | null>(null)
  const lastBassHitRef = useRef(0)
  const zoomRef = useRef(1)
  const velRef = useRef(0)

  // --- Dev-only layer toggles (Ctrl+Shift+1/2/3) ---
  const [debugLayers, setDebugLayers] = useState({ img: true, visualizer: true, mask: true })

  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      // Ctrl+Shift shortcuts (debug layers, mask editor)
      if (e.ctrlKey && e.shiftKey) {
        if (import.meta.env.DEV) {
          if (e.key === '!' || e.code === 'Digit1') {
            e.preventDefault()
            setDebugLayers(prev => ({ ...prev, img: !prev.img }))
          } else if (e.key === '@' || e.code === 'Digit2') {
            e.preventDefault()
            setDebugLayers(prev => ({ ...prev, visualizer: !prev.visualizer }))
          } else if (e.key === '#' || e.code === 'Digit3') {
            e.preventDefault()
            setDebugLayers(prev => ({ ...prev, mask: !prev.mask }))
          }
        }
        if ((e.key === 'E' || e.key === 'e') && e.code === 'KeyE') {
          e.preventDefault()
          onEditMask?.()
        }
        return
      }
      // Plain key shortcuts
      if (!e.ctrlKey && !e.shiftKey && !e.altKey && !e.metaKey) {
        if ((e.key === 'B' || e.key === 'b') && e.code === 'KeyB') {
          e.preventDefault()
          onEditBrush?.()
        }
      }
    }
    window.addEventListener('keydown', handler)
    return () => window.removeEventListener('keydown', handler)
  }, [onEditMask, onEditBrush])

  // --- Sequenced vinyl / album-art transition state ---
  const [displayedAlbum, _setDisplayedAlbum] = useState<Album>(album)
  const setDisplayedAlbum = (a: Album) => {
    _setDisplayedAlbum(a)
    onDisplayedAlbumChange?.(a)
  }
  const [vinylOut, setVinylOut] = useState(false)
  const [artEntered, setArtEntered] = useState(false)
  const [imageLoaded, setImageLoaded] = useState(false)
  const [transitioning, setTransitioning] = useState(false)

  // Snapshot segmentation for the displayed album so the foreground mask
  // persists during skip transitions instead of vanishing immediately
  // when the prop flips to null/new-album.
  const [displayedSeg, setDisplayedSeg] = useState(segmentation)

  useEffect(() => {
    if (album.id === displayedAlbum.id) {
      setDisplayedSeg(segmentation)
    }
  }, [segmentation, album.id, displayedAlbum.id])

  // When displayedAlbum actually swaps, sync to whatever segmentation is current
  useEffect(() => {
    setDisplayedSeg(segmentation)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [displayedAlbum.id])
  const pendingAlbumRef = useRef<Album | null>(null)
  const isPlayingRef = useRef(isPlaying)
  isPlayingRef.current = isPlaying

  // --- Dynamic animation durations based on transition intent ---
  const isSkip = transitionIntent === 'skip'
  const vinylDuration = isSkip ? 0.3 : 0.8
  const artDuration = isSkip ? 0.25 : 0.6

  // Keep displayedAlbum in sync for same-album updates (e.g. color changes)
  useEffect(() => {
    if (album.id === displayedAlbum.id) {
      setDisplayedAlbum(album)
    }
  }, [album, displayedAlbum.id])

  // Detect album change → begin vinyl-retract sequence
  useEffect(() => {
    if (album.id !== displayedAlbum.id) {
      pendingAlbumRef.current = album
      if (vinylOut) {
        // Vinyl is showing — retract first, then swap art on completion
        setTransitioning(true)
        setVinylOut(false)
      } else {
        // Vinyl already retracted (paused or initial) — swap art directly
        setArtEntered(false)
        setImageLoaded(false)
        setDisplayedAlbum(album)
        pendingAlbumRef.current = null
      }
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [album.id])

  // Play/pause → extend/retract vinyl during steady state
  useEffect(() => {
    if (artEntered && !pendingAlbumRef.current) {
      setVinylOut(isPlaying)
    }
  }, [isPlaying, artEntered])

  // Prefire: retract vinyl early before the track ends
  useEffect(() => {
    if (transitionIntent === 'prefire' && vinylOut && !pendingAlbumRef.current && !transitioning) {
      setTransitioning(true)
      setVinylOut(false)
    }
  }, [transitionIntent, vinylOut, transitioning])

  // Same-album skip/prefire: no art change needed — clear intent immediately
  useEffect(() => {
    if (transitionIntent && album.id === displayedAlbum.id && !pendingAlbumRef.current) {
      // Only clear if intent is 'skip' (prefire might be waiting for the track to end)
      if (transitionIntent === 'skip') {
        onTransitionDone?.()
      }
    }
  }, [transitionIntent, album.id, displayedAlbum.id, onTransitionDone])

  // Vinyl retract animation completed → swap the buffered album art
  const handleVinylAnimComplete = () => {
    if (pendingAlbumRef.current) {
      setArtEntered(false)
      setImageLoaded(false)
      setDisplayedAlbum(pendingAlbumRef.current)
      pendingAlbumRef.current = null
    }
  }

  // Album art entrance animation completed → extend vinyl
  const handleArtEnterComplete = () => {
    setArtEntered(true)
    setTransitioning(false)
    if (isPlayingRef.current) {
      setVinylOut(true)
    }
    onTransitionDone?.()
  }

  // Bass hit zoom — spring physics with direction reversal on quick follow-up hits
  useEffect(() => {
    if (!isPlaying || !settings.bassShake) {
      if (shakeRef.current) {
        shakeRef.current.style.transform = ''
        shakeRef.current.style.filter = ''
      }
      zoomRef.current = 1
      velRef.current = 0
      return
    }

    return onTick(() => {
      const energy = bassEnergyRef?.current || 0
      const now = performance.now()
      if (shakeRef.current) {
        // Detect bass hit (60ms debounce avoids multi-trigger on one transient)
        if (energy > 0.6 && now - lastBassHitRef.current > 60) {
          lastBassHitRef.current = now
          const t = (energy - 0.6) / 0.4               // normalize 0..1
          const impulse = t * t * 0.01                  // quadratic — keep zoom subtle, blur does the work

          // If zoomed in or heading there, reverse; otherwise zoom in
          if (zoomRef.current > 1.003 || velRef.current > 0.002) {
            velRef.current = -impulse
          } else {
            velRef.current = impulse
          }
        }

        // Spring physics (tuned for ~30fps tick)
        const displacement = zoomRef.current - 1
        velRef.current += -0.3 * displacement           // spring restore toward 1.0
        velRef.current *= 0.6                            // damping
        zoomRef.current += velRef.current

        // Clamp to reasonable range
        zoomRef.current = Math.max(0.97, Math.min(1.04, zoomRef.current))

        // Snap to rest when settled
        if (Math.abs(zoomRef.current - 1) < 0.0005 && Math.abs(velRef.current) < 0.0005) {
          zoomRef.current = 1
          velRef.current = 0
        }

        // Apply zoom + motion blur proportional to displacement
        const d = Math.abs(zoomRef.current - 1)
        if (d > 0.0005) {
          shakeRef.current.style.transform = `scale(${zoomRef.current.toFixed(4)})`
          shakeRef.current.style.filter = `blur(${(d * 250).toFixed(1)}px)`
        } else {
          shakeRef.current.style.transform = ''
          shakeRef.current.style.filter = ''
        }
      }
    })
  }, [isPlaying, bassEnergyRef, settings.bassShake])

  return (
    <div className="relative flex items-center justify-center w-full">
      <div
        className="relative"
        onMouseEnter={() => setIsHovered(true)}
        onMouseLeave={() => setIsHovered(false)}
      >
        {/* Vinyl disc behind album art */}
        {settings.vinylDisc && (
          <motion.div
            className="absolute inset-0 flex items-center justify-center"
            animate={{
              x: vinylOut ? 65 : 0,
              opacity: vinylOut || transitioning ? 1 : 0,
            }}
            transition={{ duration: vinylDuration, ease: [0.22, 1, 0.36, 1] }}
            onAnimationComplete={handleVinylAnimComplete}
          >
            <div
              className={`aspect-square rounded-full ${isPlaying ? 'animate-vinyl' : 'animate-vinyl-paused'}`}
              style={{
                width: '108%',
                background: `
                  radial-gradient(circle at center,
                    #111 0%,
                    #111 17%,
                    #333 17.3%,
                    #111 17.6%,
                    #1a1a1a 20%,
                    #282828 22%, #1a1a1a 22.5%,
                    #282828 25%, #1a1a1a 25.5%,
                    #282828 28%, #1a1a1a 28.5%,
                    #282828 31%, #1a1a1a 31.5%,
                    #282828 34%, #1a1a1a 34.5%,
                    #282828 37%, #1a1a1a 37.5%,
                    #282828 40%, #1a1a1a 40.5%,
                    #282828 43%, #1a1a1a 43.5%,
                    #282828 46%, #1a1a1a 46.5%,
                    #282828 49%, #1a1a1a 49.5%,
                    #282828 52%, #1a1a1a 52.5%,
                    #282828 55%, #1a1a1a 55.5%,
                    #282828 58%, #1a1a1a 58.5%,
                    #282828 61%, #1a1a1a 61.5%,
                    #282828 64%, #1a1a1a 64.5%,
                    #282828 67%, #1a1a1a 67.5%,
                    #282828 70%, #1a1a1a 70.5%,
                    #282828 73%, #1a1a1a 73.5%,
                    #282828 76%, #1a1a1a 76.5%,
                    #282828 79%, #1a1a1a 79.5%,
                    #282828 82%, #1a1a1a 82.5%,
                    #282828 85%, #1a1a1a 85.5%,
                    #282828 88%, #1a1a1a 88.5%,
                    #282828 91%, #1a1a1a 91.5%,
                    #282828 94%, #1a1a1a 94.5%,
                    #333 97%,
                    #111 100%
                  )`,
                boxShadow: `inset 0 0 30px rgba(0,0,0,0.6), 0 2px 20px rgba(0,0,0,0.5)`,
              }}
            >
              {/* Light-catch sheen — makes rotation visible */}
              <div
                className="absolute inset-0 rounded-full"
                style={{
                  background: `conic-gradient(
                    from 0deg,
                    transparent 0deg,
                    rgba(255,255,255,0.06) 40deg,
                    rgba(255,255,255,0.12) 90deg,
                    rgba(255,255,255,0.06) 140deg,
                    transparent 180deg,
                    rgba(255,255,255,0.03) 220deg,
                    rgba(255,255,255,0.08) 270deg,
                    rgba(255,255,255,0.03) 320deg,
                    transparent 360deg
                  )`,
                }}
              />
              {/* Rim highlight */}
              <div
                className="absolute inset-0 rounded-full"
                style={{
                  boxShadow: `inset 0 0 1px 1px rgba(255,255,255,0.08), inset 0 0 4px rgba(255,255,255,0.04)`,
                }}
              />
              {/* Center label */}
              <div
                className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-[20%] aspect-square rounded-full"
                style={{
                  background: `radial-gradient(circle, ${displayedAlbum.accentColor}, ${displayedAlbum.dominantColor})`,
                  boxShadow: `0 0 8px rgba(0,0,0,0.5), inset 0 1px 2px rgba(255,255,255,0.15)`,
                }}
              >
                {/* Spindle hole */}
                <div
                  className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-[14%] aspect-square rounded-full"
                  style={{ background: '#111' }}
                />
              </div>
            </div>
          </motion.div>
        )}

        {/* Album art */}
        <AnimatePresence mode="wait">
          <motion.div
            key={displayedAlbum.id}
            className="relative rounded-2xl overflow-hidden"
            style={{
              width: 'min(340px, 55vw)',
              aspectRatio: '1',
              boxShadow: settings.ambientGlow && isPlaying
                ? `0 25px 50px -12px rgba(0,0,0,0.25), 0 0 ${isLight ? '12px 3px' : '18px 4px'} color-mix(in srgb, ${displayedAlbum.accentColor} ${isLight ? '20%' : '35%'}, transparent)`
                : '0 25px 50px -12px rgba(0,0,0,0.25)',
              transition: 'box-shadow 0.4s',
            }}
            initial={{ opacity: 0, scale: 0.92, filter: 'blur(10px)' }}
            animate={{
              opacity: 1,
              scale: 1,
              filter: 'blur(0px)',
            }}
            exit={{ opacity: 0, scale: 0.95, filter: 'blur(8px)', transition: { duration: artDuration, ease: [0.22, 1, 0.36, 1] } }}
            transition={{ duration: artDuration, ease: [0.22, 1, 0.36, 1] }}
            onAnimationComplete={handleArtEnterComplete}
          >
            {/* Shake wrapper — bass zoom applies to img + visualizer + mask together */}
            <div
              ref={shakeRef}
              className="relative w-full h-full"
              style={{ transition: 'transform 100ms ease-out, filter 100ms ease-out' }}
            >
              <img
                src={displayedAlbum.art ?? undefined}
                alt={displayedAlbum.title}
                className="w-full h-full object-cover"
                draggable={false}
                onLoad={() => setImageLoaded(true)}
                style={{ opacity: debugLayers.img ? 1 : 0 }}
              />

              {/* Visualizer overlay (children slot) — z-index 1
                  Hidden until the album art image has decoded to prevent
                  bars flashing over a blank canvas on navigation. */}
              <div style={{ display: debugLayers.visualizer && imageLoaded ? 'contents' : 'none' }}>
                {children}
              </div>

              {/* Foreground mask — only show after art transition completes.
                  Uses displayedSeg (snapshotted) so the old mask persists
                  during skip transitions instead of popping off early. */}
              {displayedSeg && artEntered && imageLoaded && (
                <ForegroundMask
                  key={displayedAlbum.id}
                  segmentation={displayedSeg}
                  style={{ opacity: debugLayers.mask ? 1 : 0 }}
                />
              )}
            </div>

            {/* Hover overlay with subtle inner glow */}
            <motion.div
              className="absolute inset-0 rounded-2xl pointer-events-none"
              animate={{ opacity: isHovered ? 1 : 0 }}
              transition={{ duration: 0.3 }}
              style={{
                boxShadow: `inset 0 0 60px color-mix(in srgb, ${displayedAlbum.accentColor} ${isLight ? '19%' : '8%'}, transparent)`,
                zIndex: 3,
              }}
            />

          </motion.div>
        </AnimatePresence>

        {/* Segmentation processing indicator */}
        {segmentationLoading && (
          <div
            className="absolute top-2 right-2 z-50 pointer-events-none flex items-center gap-1.5 px-2 py-1 rounded-md"
            style={{ background: 'rgba(0,0,0,0.55)', backdropFilter: 'blur(4px)' }}
          >
            <svg className="animate-spin" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round">
              <path d="M12 2a10 10 0 0 1 10 10" />
            </svg>
            <span style={{ fontFamily: 'var(--font-mono)', fontSize: 10, color: '#ccc' }}>
              Processing
            </span>
          </div>
        )}

        {/* Edit mask button — only when depth layers active and segmentation present */}
        {depthLayerActive && !segmentationLoading && (
          <div className="absolute top-2 right-2 z-50 flex items-center gap-1.5">
            {hasOverride && (
              <div
                className="pointer-events-none flex items-center gap-1 px-1.5 py-0.5 rounded-md transition-opacity"
                style={{ background: 'rgba(0,0,0,0.55)', backdropFilter: 'blur(4px)', opacity: isHovered ? 1 : 0 }}
              >
                <span style={{ fontFamily: 'var(--font-mono)', fontSize: 9, color: 'var(--accent)' }}>
                  Custom
                </span>
              </div>
            )}
            <button
              onClick={onEditBrush}
              className="flex items-center justify-center w-7 h-7 rounded-md transition-opacity"
              style={{ background: 'rgba(0,0,0,0.55)', backdropFilter: 'blur(4px)', color: '#ccc', opacity: isHovered ? 1 : 0 }}
              title="Paint mask (B)"
              aria-label="Paint mask"
            >
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <path d="M18.37 2.63a2.12 2.12 0 0 1 3 3L14 13l-4 1 1-4 7.37-7.37z" />
                <path d="M9 2H4a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-5" />
                <path d="M14 13l-1.5 5.5L8 22" />
              </svg>
            </button>
            <button
              onClick={onEditMask}
              className="flex items-center justify-center w-7 h-7 rounded-md transition-opacity"
              style={{ background: 'rgba(0,0,0,0.55)', backdropFilter: 'blur(4px)', color: '#ccc', opacity: isHovered ? 1 : 0 }}
              title="Edit mask (Ctrl+Shift+E)"
              aria-label="Edit mask"
            >
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7" />
                <path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z" />
              </svg>
            </button>
          </div>
        )}

        {/* Dev-only layer visibility indicator */}
        {import.meta.env.DEV && (!debugLayers.img || !debugLayers.visualizer || !debugLayers.mask) && (
          <div
            className="absolute top-2 left-2 flex gap-1.5 z-50 pointer-events-none"
            style={{ fontFamily: 'var(--font-mono)', fontSize: 10 }}
          >
            <span style={{ color: debugLayers.img ? '#4f4' : '#f44', opacity: 0.9 }}>
              1:IMG
            </span>
            <span style={{ color: debugLayers.visualizer ? '#4f4' : '#f44', opacity: 0.9 }}>
              2:VIZ
            </span>
            <span style={{ color: debugLayers.mask ? '#4f4' : '#f44', opacity: 0.9 }}>
              3:MASK
            </span>
          </div>
        )}
      </div>

      {/* Subtle reflection beneath */}
      <motion.div
        className="absolute -bottom-8 w-[60%] h-12 rounded-full blur-2xl"
        style={{
          background: `linear-gradient(to right, transparent, color-mix(in srgb, ${displayedAlbum.dominantColor} ${isLight ? '31%' : '19%'}, transparent), transparent)`,
        }}
        animate={{ opacity: isPlaying ? 0.5 : 0.2 }}
        transition={{ duration: 1 }}
      />
    </div>
  )
})
