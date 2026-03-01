import { useRef, useEffect, useCallback } from 'react'
import { motion } from 'motion/react'
import { usePlayback } from '../contexts/playback/PlaybackContext'
import { PlayIcon, PauseIcon, SkipForwardIcon } from './Icons'
import { playbackTimeStore } from '../lib/playbackTimeStore'
import { perfCountRender } from '../lib/perf'

interface MiniPlayerProps {
  onExpand: () => void
}

export default function MiniPlayer({ onExpand }: MiniPlayerProps) {
  perfCountRender('MiniPlayer')
  const { currentTrack: track, currentAlbum: album, isPlaying, handlePlayPause, handleNext, seek } = usePlayback()

  const progressRef = useRef<HTMLDivElement | null>(null)
  const trackRef = useRef<HTMLDivElement | null>(null)
  const scrubbingRef = useRef(false)

  // Subscribe to playbackTimeStore imperatively — update DOM directly
  useEffect(() => {
    return playbackTimeStore.subscribe(() => {
      if (scrubbingRef.current) return
      const t = playbackTimeStore.get()
      const pct = track?.duration ? (t / track.duration) * 100 : 0
      if (progressRef.current) progressRef.current.style.transform = `scaleX(${pct / 100})`
    })
  }, [track?.duration])

  const getProgressFromEvent = useCallback((e: React.PointerEvent<HTMLDivElement>) => {
    if (!trackRef.current) return 0
    const rect = trackRef.current.getBoundingClientRect()
    return Math.min(100, Math.max(0, ((e.clientX - rect.left) / rect.width) * 100))
  }, [])

  const handleProgressPointerDown = useCallback((e: React.PointerEvent<HTMLDivElement>) => {
    e.preventDefault()
    e.stopPropagation()
    scrubbingRef.current = true
    const pct = getProgressFromEvent(e)
    if (progressRef.current) progressRef.current.style.transform = `scaleX(${pct / 100})`
    trackRef.current?.setPointerCapture(e.pointerId)
  }, [getProgressFromEvent])

  const handleProgressPointerMove = useCallback((e: React.PointerEvent<HTMLDivElement>) => {
    if (!scrubbingRef.current) return
    const pct = getProgressFromEvent(e)
    if (progressRef.current) progressRef.current.style.transform = `scaleX(${pct / 100})`
  }, [getProgressFromEvent])

  const handleProgressPointerUp = useCallback((e: React.PointerEvent<HTMLDivElement>) => {
    if (!scrubbingRef.current) return
    scrubbingRef.current = false
    const pct = getProgressFromEvent(e)
    if (track?.duration) seek((pct / 100) * track.duration)
  }, [getProgressFromEvent, track?.duration, seek])

  if (!track || !album) return null

  const initialProgress = track?.duration ? (playbackTimeStore.get() / track.duration) * 100 : 0

  return (
    <div
      className="shrink-0 relative select-none glass"
      style={{ height: 72, background: 'var(--glass-bg-surface)', borderTop: '1px solid var(--border-subtle)' }}
    >
      {/* Progress bar */}
      <div
        ref={trackRef}
        className="absolute left-0 right-0 z-10 cursor-pointer group/prog touch-none"
        style={{ top: -8, height: 20 }}
        onClick={(e: React.MouseEvent) => e.stopPropagation()}
        onPointerDown={handleProgressPointerDown}
        onPointerMove={handleProgressPointerMove}
        onPointerUp={handleProgressPointerUp}
      >
        <div
          className="absolute left-0 right-0 h-[2px] group-hover/prog:h-[4px] transition-all duration-150"
          style={{ background: 'var(--border-subtle)', top: 8 }}
        >
          <div
            ref={progressRef}
            className="h-full"
            style={{
              background: 'var(--accent)',
              width: '100%',
              transform: `scaleX(${initialProgress / 100})`,
              transformOrigin: 'left',
              transition: 'none',
            }}
          />
        </div>
      </div>

      <div className="flex items-center h-full px-4 gap-3 cursor-pointer" onClick={onExpand}>
        {/* Album art */}
        <div className="w-12 h-12 rounded-lg overflow-hidden shrink-0" style={{ background: 'var(--bg-elevated)' }}>
          {album.art && (
            <img src={album.art} alt="" className="w-full h-full object-cover" draggable={false} />
          )}
        </div>

        {/* Track info */}
        <div className="flex-1 min-w-0">
          <p className="text-sm font-medium truncate" style={{ color: 'var(--text-primary)' }}>
            {track.title}
          </p>
          <p className="text-xs truncate" style={{ color: 'var(--text-tertiary)' }}>
            {track.artist}
          </p>
        </div>

        {/* Controls */}
        <div className="flex items-center gap-1 shrink-0">
          <motion.button
            onClick={(e: React.MouseEvent) => { e.stopPropagation(); (e.currentTarget as HTMLButtonElement).blur(); handlePlayPause() }}
            className="no-drag flex items-center justify-center w-10 h-10 rounded-full"
            style={{ color: 'var(--text-primary)' }}
            aria-label={isPlaying ? 'Pause' : 'Play'}
            whileHover={{ scale: 1.1, background: 'var(--bg-hover)' }}
            whileTap={{ scale: 0.9 }}
          >
            {isPlaying ? <PauseIcon size={20} /> : <PlayIcon size={20} />}
          </motion.button>
          <motion.button
            onClick={(e: React.MouseEvent) => { e.stopPropagation(); (e.currentTarget as HTMLButtonElement).blur(); handleNext() }}
            className="no-drag flex items-center justify-center w-10 h-10 rounded-full"
            style={{ color: 'var(--text-secondary)' }}
            aria-label="Next track"
            whileHover={{ scale: 1.1, background: 'var(--bg-hover)' }}
            whileTap={{ scale: 0.9 }}
          >
            <SkipForwardIcon size={18} />
          </motion.button>
        </div>
      </div>
    </div>
  )
}
