import { useState, useRef, useCallback, useEffect } from 'react'
import { setAnalyserVolume } from '../lib/audioAnalyser'
import { playbackTimeStore } from '../lib/playbackTimeStore'

/**
 * Hook that manages a single HTMLAudioElement for real audio playback.
 * Falls back gracefully in browser mode (no filePath = no audio).
 *
 * currentTime is written to playbackTimeStore (external store) instead of
 * React state, so only subscribing components re-render at 15fps.
 */
export default function useAudioPlayer({ onEnded, onAboutToEnd, onError }: { onEnded?: () => void; onAboutToEnd?: () => void; onError?: () => void } = {}) {
  const audioRef = useRef<HTMLAudioElement | null>(null)
  const onEndedRef = useRef<(() => void) | undefined>(onEnded)
  const onAboutToEndRef = useRef<(() => void) | undefined>(onAboutToEnd)
  const onErrorRef = useRef<(() => void) | undefined>(onError)
  const aboutToEndFiredRef = useRef(false)
  const seekingRef = useRef<boolean>(false)
  const canPlayListenerRef = useRef<(() => void) | null>(null)
  const seekTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null)

  const [duration, setDuration] = useState<number>(0)
  const [isPlaying, setIsPlaying] = useState<boolean>(false)

  // Keep callback refs current without re-subscribing listeners
  useEffect(() => {
    onEndedRef.current = onEnded
  }, [onEnded])

  useEffect(() => {
    onAboutToEndRef.current = onAboutToEnd
  }, [onAboutToEnd])

  useEffect(() => {
    onErrorRef.current = onError
  }, [onError])

  // Create audio element once
  useEffect(() => {
    const audio = new Audio()
    audio.crossOrigin = 'anonymous'
    audioRef.current = audio

    const handleLoadedMetadata = () => {
      setDuration(audio.duration)
    }

    const handleEnded = () => {
      setIsPlaying(false)
      playbackTimeStore.set(0)
      onEndedRef.current?.()
    }

    const handlePlay = () => setIsPlaying(true)
    const handlePause = () => setIsPlaying(false)
    const handleSeeked = () => {
      seekingRef.current = false
    }

    const handleError = () => {
      setIsPlaying(false)
      onErrorRef.current?.()
    }

    audio.addEventListener('loadedmetadata', handleLoadedMetadata)
    audio.addEventListener('ended', handleEnded)
    audio.addEventListener('play', handlePlay)
    audio.addEventListener('pause', handlePause)
    audio.addEventListener('seeked', handleSeeked)
    audio.addEventListener('error', handleError)

    return () => {
      audio.removeEventListener('loadedmetadata', handleLoadedMetadata)
      audio.removeEventListener('ended', handleEnded)
      audio.removeEventListener('play', handlePlay)
      audio.removeEventListener('pause', handlePause)
      audio.removeEventListener('seeked', handleSeeked)
      audio.removeEventListener('error', handleError)
      audio.pause()
      audio.src = ''
      if (seekTimeoutRef.current) clearTimeout(seekTimeoutRef.current)
    }
  }, [])

  // Update currentTime via native timeupdate event (~4fps, no JS timers needed)
  useEffect(() => {
    const audio = audioRef.current
    if (!audio || !isPlaying) return
    const handleTimeUpdate = () => {
      if (!seekingRef.current) {
        playbackTimeStore.set(audio.currentTime)
      }
      // Pre-fire "about to end" callback ~1.5s before track finishes
      if (!aboutToEndFiredRef.current && audio.duration > 0) {
        const remaining = audio.duration - audio.currentTime
        if (remaining > 0 && remaining <= 1.5) {
          aboutToEndFiredRef.current = true
          onAboutToEndRef.current?.()
        }
      }
    }
    audio.addEventListener('timeupdate', handleTimeUpdate)
    return () => audio.removeEventListener('timeupdate', handleTimeUpdate)
  }, [isPlaying])

  const play = useCallback((filePath: string) => {
    const audio = audioRef.current
    if (!audio || !filePath) return

    aboutToEndFiredRef.current = false

    // Remove any stale canplay listener from a prior load() call
    if (canPlayListenerRef.current) {
      audio.removeEventListener('canplay', canPlayListenerRef.current)
      canPlayListenerRef.current = null
    }

    const src = `audio://track/${encodeURIComponent(filePath)}`
    audio.src = src
    playbackTimeStore.set(0)
    audio.play().catch((err: DOMException) => {
      if (err.name === 'NotAllowedError') return // Autoplay blocked; user interaction will retry
      console.warn('[audio] playback failed:', err.name, err.message)
    })
  }, [])

  // Load a track without playing (for restoring state on startup)
  const load = useCallback((filePath: string, seekTo: number = 0) => {
    const audio = audioRef.current
    if (!audio || !filePath) return

    aboutToEndFiredRef.current = false
    const src = `audio://track/${encodeURIComponent(filePath)}`
    audio.src = src

    // Remove any stale canplay listener from a previous load() call
    if (canPlayListenerRef.current) {
      audio.removeEventListener('canplay', canPlayListenerRef.current)
      canPlayListenerRef.current = null
    }

    if (seekTo > 0) {
      const onCanPlay = () => {
        audio.currentTime = seekTo
        playbackTimeStore.set(seekTo)
        audio.removeEventListener('canplay', onCanPlay)
        canPlayListenerRef.current = null
      }
      canPlayListenerRef.current = onCanPlay
      audio.addEventListener('canplay', onCanPlay)
    } else {
      playbackTimeStore.set(0)
    }
  }, [])

  const pause = useCallback(() => {
    audioRef.current?.pause()
  }, [])

  const resume = useCallback(() => {
    audioRef.current?.play().catch((err) => {
      // Autoplay may be blocked; user interaction will retry
      if (import.meta.env.DEV) console.warn('resume() blocked:', err)
    })
  }, [])

  const seek = useCallback((seconds: number) => {
    const audio = audioRef.current
    if (!audio || !audio.src) return
    seekingRef.current = true
    audio.currentTime = seconds
    playbackTimeStore.set(seconds)
    // Fallback: clear seeking flag after 500ms if 'seeked' event never fires
    if (seekTimeoutRef.current) clearTimeout(seekTimeoutRef.current)
    seekTimeoutRef.current = setTimeout(() => { seekingRef.current = false }, 500)
  }, [])

  const setVolume = useCallback((vol: number) => {
    // Keep audioElement.volume at 1 so the AnalyserNode always sees
    // full-amplitude data for the visualizer. Control actual playback
    // volume via the Web Audio GainNode instead.
    if (audioRef.current) {
      audioRef.current.volume = 1
    }
    setAnalyserVolume(Math.max(0, Math.min(1, vol)))
  }, [])

  return {
    play,
    load,
    pause,
    resume,
    seek,
    setVolume,
    getCurrentTime: playbackTimeStore.get,
    duration,
    isPlaying,
    audioRef,
  }
}
