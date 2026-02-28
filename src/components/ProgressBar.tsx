import { useState, useRef, useCallback, useEffect } from 'react'
import { formatTime } from '../lib/formatTime'
import { playbackTimeStore } from '../lib/playbackTimeStore'
import { perfCountRender } from '../lib/perf'

interface ProgressBarProps {
  duration: number
  onSeek: (time: number) => void
}

export default function ProgressBar({ duration, onSeek }: ProgressBarProps) {
  perfCountRender('ProgressBar')

  const [scrubbing, setScrubbing] = useState<boolean>(false)
  const [scrubProgress, setScrubProgress] = useState<number>(0)
  const trackRef = useRef<HTMLDivElement | null>(null)
  const fillRef = useRef<HTMLDivElement | null>(null)
  const thumbRef = useRef<HTMLDivElement | null>(null)
  const timeRef = useRef<HTMLSpanElement | null>(null)
  const scrubbingRef = useRef<boolean>(false)

  // Keep scrubbingRef in sync so the store listener can read it without
  // causing the effect to re-subscribe on every scrub state change.
  useEffect(() => { scrubbingRef.current = scrubbing }, [scrubbing])

  // Subscribe to playbackTimeStore imperatively — update DOM directly,
  // no React re-renders during playback.
  useEffect(() => {
    return playbackTimeStore.subscribe(() => {
      if (scrubbingRef.current) return
      const t = playbackTimeStore.get()
      const pct = duration > 0 ? (t / duration) * 100 : 0
      if (fillRef.current) fillRef.current.style.transform = `scaleX(${pct / 100})`
      if (thumbRef.current) thumbRef.current.style.left = `${pct}%`
      if (timeRef.current) timeRef.current.textContent = formatTime(t)
    })
  }, [duration])

  const displayProgress = scrubbing ? scrubProgress : (duration > 0 ? (playbackTimeStore.get() / duration) * 100 : 0)

  const clamp = (v: number, min: number, max: number): number => Math.min(max, Math.max(min, v))

  const getProgressFromEvent = useCallback((e: React.PointerEvent<HTMLDivElement>) => {
    const rect = trackRef.current!.getBoundingClientRect()
    return clamp(((e.clientX - rect.left) / rect.width) * 100, 0, 100)
  }, [])

  const handlePointerDown = useCallback((e: React.PointerEvent<HTMLDivElement>) => {
    e.preventDefault()
    const pct = getProgressFromEvent(e)
    setScrubbing(true)
    setScrubProgress(pct)
    trackRef.current!.setPointerCapture(e.pointerId)
  }, [getProgressFromEvent])

  const handlePointerMove = useCallback((e: React.PointerEvent<HTMLDivElement>) => {
    if (!scrubbing) return
    const pct = getProgressFromEvent(e)
    setScrubProgress(pct)
    if (fillRef.current) fillRef.current.style.transform = `scaleX(${pct / 100})`
    if (thumbRef.current) thumbRef.current.style.left = `${pct}%`
    if (timeRef.current) timeRef.current.textContent = formatTime((pct / 100) * duration)
  }, [scrubbing, getProgressFromEvent, duration])

  const handlePointerUp = useCallback((e: React.PointerEvent<HTMLDivElement>) => {
    if (!scrubbing) return
    const pct = getProgressFromEvent(e)
    setScrubbing(false)
    onSeek((pct / 100) * duration)
  }, [scrubbing, getProgressFromEvent, onSeek, duration])

  return (
    <div className="w-full max-w-md mx-auto px-4">
      <div className="relative group">
        {/* Invisible wider hit area for easier clicking/dragging */}
        <div
          ref={trackRef}
          className="w-full py-3 cursor-pointer touch-none"
          onPointerDown={handlePointerDown}
          onPointerMove={handlePointerMove}
          onPointerUp={handlePointerUp}
        >
          {/* Visible thin track */}
          <div
            className="w-full h-1 rounded-full overflow-hidden"
            style={{ background: 'var(--border)' }}
          >
            {/* Filled progress — scaleX avoids layout recalculation */}
            <div
              ref={fillRef}
              className="h-full rounded-full"
              style={{
                background: 'var(--text-primary)',
                width: '100%',
                transform: `scaleX(${displayProgress / 100})`,
                transformOrigin: 'left',
                transition: 'none',
              }}
            />
          </div>
        </div>

        {/* Glow on the leading edge */}
        <div
          className="absolute top-1/2 -translate-y-1/2 w-6 h-6 rounded-full blur-md opacity-0 group-hover:opacity-50 transition-opacity pointer-events-none"
          style={{ background: 'var(--accent)', left: `${displayProgress}%` }}
        />

        {/* Scrubber thumb — visible on hover or while scrubbing */}
        <div
          ref={thumbRef}
          className={`absolute top-1/2 -translate-y-1/2 w-3 h-3 rounded-full transition-opacity pointer-events-none ${scrubbing ? 'opacity-100' : 'opacity-0 group-hover:opacity-100'}`}
          style={{
            left: `${displayProgress}%`,
            marginLeft: '-6px',
            background: 'var(--text-primary)',
            boxShadow: '0 0 8px var(--accent)',
          }}
        />
      </div>

      {/* Time display */}
      <div className="flex justify-between mt-2">
        <span
          ref={timeRef}
          className="font-mono text-[11px] tabular-nums"
          style={{ color: 'var(--text-tertiary)' }}
        >
          {scrubbing ? formatTime((scrubProgress / 100) * duration) : formatTime(playbackTimeStore.get())}
        </span>
        <span
          className="font-mono text-[11px] tabular-nums"
          style={{ color: 'var(--text-tertiary)' }}
        >
          {formatTime(duration)}
        </span>
      </div>
    </div>
  )
}
