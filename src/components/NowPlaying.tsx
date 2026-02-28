import { memo, useRef, useState, useLayoutEffect, useEffect, useCallback } from 'react'
import { motion, AnimatePresence, useMotionValue, useTransform, animate } from 'motion/react'
import type { MotionValue } from 'motion/react'
import { HeartIcon } from './Icons'
import AddToPlaylistMenu from './AddToPlaylistMenu'
import { perfCountRender } from '../lib/perf'
import type { Track, Album, Playlist } from '../types'

const FADE_WIDTH = 12

interface MarqueeTextProps {
  text: string
  className?: string
  style?: React.CSSProperties
}

function MarqueeText({ text, className = '', style }: MarqueeTextProps) {
  const containerRef = useRef<HTMLDivElement | null>(null)
  const textRef = useRef<HTMLSpanElement | null>(null)
  const [overflow, setOverflow] = useState(0)

  const measure = useCallback(() => {
    if (containerRef.current && textRef.current) {
      const diff = textRef.current.scrollWidth - containerRef.current.clientWidth
      setOverflow(diff > 0 ? diff : 0)
    }
  }, [])

  useLayoutEffect(measure, [text, measure])

  // Re-measure when the container becomes visible (e.g. parent goes from
  // display:none → visible) so the marquee starts even if the text changed
  // while the Now Playing view was hidden.
  useEffect(() => {
    const el = containerRef.current
    if (!el) return
    const ro = new ResizeObserver(measure)
    ro.observe(el)
    return () => ro.disconnect()
  }, [measure])

  const needsMarquee = overflow > 0
  const duration = needsMarquee ? 2 + overflow / 60 : 0

  const x = useMotionValue(0)

  const maskImage: MotionValue<string> = useTransform(x, (xVal: number) => {
    if (overflow <= 0) return 'none'
    const left = Math.round(Math.min(-xVal, FADE_WIDTH))
    const right = Math.round(Math.min(overflow + xVal, FADE_WIDTH))
    return `linear-gradient(to right, transparent, black ${left}px, black calc(100% - ${right}px), transparent)`
  })

  useEffect(() => {
    if (!needsMarquee || overflow <= 0) {
      x.set(0)
      return
    }
    const controls = animate(x, [0, -overflow], {
      duration,
      delay: 1.5,
      repeat: Infinity,
      repeatType: 'reverse',
      repeatDelay: 2,
      ease: 'easeInOut',
    })
    return () => controls.stop()
  }, [needsMarquee, overflow])

  return (
    <motion.div
      ref={containerRef}
      className={`overflow-hidden whitespace-nowrap ${className}`}
      style={{ ...style, maskImage, WebkitMaskImage: maskImage }}
    >
      <motion.span
        ref={textRef}
        style={{ x, display: 'inline-block' }}
      >
        {text}
      </motion.span>
    </motion.div>
  )
}

interface NowPlayingProps {
  track: Track
  album: Album
  isTrackFavorited?: (trackId: string) => boolean
  onToggleFavorite?: (trackId: string) => void
  onAddToPlaylist?: (playlistId: string, trackId: string) => void
  onCreatePlaylist?: (name: string) => Playlist | null
  playlists?: Playlist[]
}

export default memo(function NowPlaying({
  track,
  album,
  isTrackFavorited,
  onToggleFavorite,
  onAddToPlaylist,
  onCreatePlaylist,
  playlists,
}: NowPlayingProps) {
  perfCountRender('NowPlaying')
  const isFav = isTrackFavorited?.(track.id) ?? false

  return (
    <div className="relative w-full max-w-md mx-auto px-4">
      <div className="text-center px-12">
        <AnimatePresence mode="wait">
          <motion.div
            key={track.id}
            initial={{ opacity: 0, y: 12 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -8 }}
            transition={{ duration: 0.4, ease: [0.22, 1, 0.36, 1] }}
          >
            <MarqueeText
              text={track.title}
              className="font-display text-lg font-semibold"
              style={{ color: 'var(--text-primary)' }}
            />
          </motion.div>
        </AnimatePresence>

        <AnimatePresence mode="wait">
          <motion.div
            key={`${track.id}-artist`}
            initial={{ opacity: 0, y: 8 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -6 }}
            transition={{ duration: 0.4, delay: 0.08, ease: [0.22, 1, 0.36, 1] }}
          >
            <p className="text-sm mt-0.5" style={{ color: 'var(--text-secondary)' }}>
              {track.artist}
              <span className="mx-1.5" style={{ color: 'var(--text-tertiary)' }}>
                &middot;
              </span>
              <span style={{ color: 'var(--text-tertiary)' }}>{album.title}</span>
            </p>
          </motion.div>
        </AnimatePresence>
      </div>

      <div className="absolute right-4 top-1/2 -translate-y-1/2 flex items-center gap-1">
        <motion.button
          onClick={() => onToggleFavorite?.(track.id)}
          className="no-drag"
          aria-label={isFav ? 'Remove from favorites' : 'Add to favorites'}
          style={{ color: isFav ? 'var(--accent)' : 'var(--text-tertiary)' }}
          whileHover={{ scale: 1.15 }}
          whileTap={{ scale: 0.85 }}
        >
          <HeartIcon size={18} filled={isFav} />
        </motion.button>

        {onAddToPlaylist && playlists && (
          <AddToPlaylistMenu
            trackId={track.id}
            playlists={playlists}
            onAddToPlaylist={onAddToPlaylist}
            onCreatePlaylist={onCreatePlaylist!}
            alwaysVisible
          />
        )}
      </div>
    </div>
  )
})
