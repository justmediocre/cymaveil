import { useState, useCallback, useRef, useEffect } from 'react'
import { motion, AnimatePresence } from 'motion/react'
import { VolumeIcon } from './Icons'

interface VolumeControlProps {
  volume?: number
  onVolumeChange?: (volume: number) => void
}

const TRACK_HEIGHT = 96
const THUMB_SIZE = 12

export default function VolumeControl({ volume = 75, onVolumeChange }: VolumeControlProps) {
  const [prevVolume, setPrevVolume] = useState<number>(75)
  const [open, setOpen] = useState(false)
  const containerRef = useRef<HTMLDivElement | null>(null)
  const trackRef = useRef<HTMLDivElement | null>(null)
  const dragging = useRef(false)
  const closeTimer = useRef<ReturnType<typeof setTimeout> | null>(null)
  const volumeRef = useRef(volume)
  volumeRef.current = volume

  const getLevel = (): 'high' | 'low' | 'mute' => {
    if (volume === 0) return 'mute'
    if (volume < 50) return 'low'
    return 'high'
  }

  const setVolume = useCallback((val: number) => {
    onVolumeChange?.(Math.max(0, Math.min(100, val)))
  }, [onVolumeChange])

  const toggleMute = () => {
    if (volume > 0) {
      setPrevVolume(volume)
      setVolume(0)
    } else {
      setVolume(prevVolume)
    }
  }

  // Scroll wheel on the whole control area
  useEffect(() => {
    const el = containerRef.current
    if (!el) return
    const handleWheel = (e: WheelEvent) => {
      e.preventDefault()
      const delta = e.deltaY > 0 ? -5 : 5
      onVolumeChange?.(Math.max(0, Math.min(100, volume + delta)))
    }
    el.addEventListener('wheel', handleWheel, { passive: false })
    return () => el.removeEventListener('wheel', handleWheel)
  }, [volume, onVolumeChange])

  // Close when clicking outside
  useEffect(() => {
    if (!open) return
    const handleClick = (e: MouseEvent) => {
      if (containerRef.current && !containerRef.current.contains(e.target as Node)) {
        setOpen(false)
      }
    }
    document.addEventListener('pointerdown', handleClick)
    return () => document.removeEventListener('pointerdown', handleClick)
  }, [open])

  const cancelClose = () => {
    if (closeTimer.current) {
      clearTimeout(closeTimer.current)
      closeTimer.current = null
    }
  }

  const scheduleClose = () => {
    if (dragging.current) return
    cancelClose()
    closeTimer.current = setTimeout(() => setOpen(false), 400)
  }

  // Drag handling for the vertical slider
  const volumeFromY = useCallback((clientY: number) => {
    const track = trackRef.current
    if (!track) return volumeRef.current
    const rect = track.getBoundingClientRect()
    const ratio = 1 - (clientY - rect.top) / rect.height
    return Math.max(0, Math.min(100, Math.round(ratio * 100)))
  }, [])

  const onPointerDown = useCallback((e: React.PointerEvent) => {
    e.preventDefault()
    dragging.current = true
    cancelClose()
    setVolume(volumeFromY(e.clientY))
    const onMove = (ev: PointerEvent) => setVolume(volumeFromY(ev.clientY))
    const onUp = () => {
      dragging.current = false
      document.removeEventListener('pointermove', onMove)
      document.removeEventListener('pointerup', onUp)
    }
    document.addEventListener('pointermove', onMove)
    document.addEventListener('pointerup', onUp)
  }, [volumeFromY, setVolume])

  const filledHeight = `${volume}%`
  const thumbBottom = `calc(${volume}% - ${THUMB_SIZE / 2}px)`

  return (
    <div
      ref={containerRef}
      className="relative"
      onMouseEnter={() => { cancelClose(); setOpen(true) }}
      onMouseLeave={scheduleClose}
    >
      {/* Popup slider */}
      <AnimatePresence>
        {open && (
          <motion.div
            initial={{ opacity: 0, scale: 0.9, y: 4 }}
            animate={{ opacity: 1, scale: 1, y: 0 }}
            exit={{ opacity: 0, scale: 0.9, y: 4 }}
            transition={{ duration: 0.15, ease: 'easeOut' }}
            className="glass absolute bottom-full left-1/2 -translate-x-1/2 mb-2 rounded-xl px-3 py-3 flex flex-col items-center"
            style={{ background: 'var(--glass-bg-surface)', zIndex: 50 }}
          >
            {/* Vertical track area */}
            <div
              className="relative cursor-pointer"
              style={{ width: THUMB_SIZE + 8, height: TRACK_HEIGHT }}
              onPointerDown={onPointerDown}
            >
              {/* Background track */}
              <div
                ref={trackRef}
                className="absolute left-1/2 -translate-x-1/2 rounded-full"
                style={{
                  width: 4,
                  height: TRACK_HEIGHT,
                  background: 'var(--border)',
                }}
              />
              {/* Filled track */}
              <div
                className="absolute left-1/2 -translate-x-1/2 bottom-0 rounded-full transition-[height] duration-75"
                style={{
                  width: 4,
                  height: filledHeight,
                  background: 'var(--text-secondary)',
                }}
              />
              {/* Thumb */}
              <div
                className="absolute left-1/2 -translate-x-1/2 rounded-full transition-[bottom] duration-75"
                style={{
                  width: THUMB_SIZE,
                  height: THUMB_SIZE,
                  bottom: thumbBottom,
                  background: 'var(--text-primary)',
                }}
              />
            </div>
          </motion.div>
        )}
      </AnimatePresence>

      {/* Volume icon button */}
      <motion.button
        onClick={toggleMute}
        className="no-drag flex items-center justify-center w-10 h-10 rounded-full"
        aria-label={volume === 0 ? 'Unmute' : 'Mute'}
        style={{ color: 'var(--text-tertiary)' }}
        whileHover={{ color: 'var(--text-primary)' }}
        whileTap={{ scale: 0.9 }}
      >
        <VolumeIcon size={20} level={getLevel()} />
      </motion.button>
    </div>
  )
}
