import { useState, useRef, useCallback, useEffect } from 'react'
import { setAnalyserVolume, setDeckFadeGain, cancelFadeRamps } from '../lib/audioAnalyser'
import { playbackTimeStore } from '../lib/playbackTimeStore'
import type { DeckId } from '../lib/audioAnalyser'

/**
 * Hook that manages dual HTMLAudioElements (A/B decks) for crossfade playback.
 * Falls back gracefully in browser mode (no filePath = no audio).
 *
 * currentTime is written to playbackTimeStore (external store) instead of
 * React state, so only subscribing components re-render at 15fps.
 */
export default function useAudioPlayer({
  onEnded,
  onAboutToEnd,
  onError,
  aboutToEndThreshold = 1.5,
}: {
  onEnded?: () => void
  onAboutToEnd?: () => void
  onError?: () => void
  aboutToEndThreshold?: number
} = {}) {
  const deckARef = useRef<HTMLAudioElement | null>(null)
  const deckBRef = useRef<HTMLAudioElement | null>(null)
  const activeDeckRef = useRef<DeckId>('A')
  const crossfadingRef = useRef(false)
  const crossfadeTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null)

  // audioRef always points to the active deck (external API unchanged)
  const audioRef = useRef<HTMLAudioElement | null>(null)
  // secondaryAudioRef points to the inactive deck (for analyser init)
  const secondaryAudioRef = useRef<HTMLAudioElement | null>(null)

  const onEndedRef = useRef<(() => void) | undefined>(onEnded)
  const onAboutToEndRef = useRef<(() => void) | undefined>(onAboutToEnd)
  const onErrorRef = useRef<(() => void) | undefined>(onError)
  const aboutToEndFiredRef = useRef(false)
  const aboutToEndThresholdRef = useRef(aboutToEndThreshold)
  const seekingRef = useRef<boolean>(false)
  const canPlayListenerRef = useRef<(() => void) | null>(null)
  const seekTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null)

  const [duration, setDuration] = useState<number>(0)
  const [isPlaying, setIsPlaying] = useState<boolean>(false)

  // Keep callback refs current without re-subscribing listeners
  useEffect(() => { onEndedRef.current = onEnded }, [onEnded])
  useEffect(() => { onAboutToEndRef.current = onAboutToEnd }, [onAboutToEnd])
  useEffect(() => { onErrorRef.current = onError }, [onError])
  useEffect(() => { aboutToEndThresholdRef.current = aboutToEndThreshold }, [aboutToEndThreshold])

  const getActiveDeck = useCallback(() => activeDeckRef.current === 'A' ? deckARef.current : deckBRef.current, [])
  const getInactiveDeck = useCallback(() => activeDeckRef.current === 'A' ? deckBRef.current : deckARef.current, [])
  const getInactiveDeckId = useCallback((): DeckId => activeDeckRef.current === 'A' ? 'B' : 'A', [])

  /**
   * Snap any in-progress crossfade to completion instantly.
   * Called when the user manually interacts (skip, pause, seek) during a crossfade.
   */
  const finalizeCrossfade = useCallback(() => {
    if (!crossfadingRef.current) return

    crossfadingRef.current = false
    if (crossfadeTimerRef.current) {
      clearTimeout(crossfadeTimerRef.current)
      crossfadeTimerRef.current = null
    }

    cancelFadeRamps()

    // The incoming deck is already the "active" one (swapped at crossfade start)
    // Snap gains: active = 1, inactive = 0
    setDeckFadeGain(activeDeckRef.current, 1)
    setDeckFadeGain(getInactiveDeckId(), 0)

    // Pause the outgoing (now inactive) deck
    const outgoing = getInactiveDeck()
    if (outgoing) {
      outgoing.pause()
      outgoing.src = ''
    }
  }, [getInactiveDeck, getInactiveDeckId])

  // Create both audio elements once
  useEffect(() => {
    const deckA = new Audio()
    deckA.crossOrigin = 'anonymous'
    const deckB = new Audio()
    deckB.crossOrigin = 'anonymous'

    deckARef.current = deckA
    deckBRef.current = deckB
    audioRef.current = deckA
    secondaryAudioRef.current = deckB

    // --- Deck A event listeners (primary — drives state) ---

    const handleLoadedMetadata = () => {
      if (activeDeckRef.current === 'A') {
        setDuration(deckA.duration)
      }
    }

    const handleEnded = () => {
      if (activeDeckRef.current === 'A') {
        setIsPlaying(false)
        playbackTimeStore.set(0)
        onEndedRef.current?.()
      }
    }

    const handlePlay = () => {
      if (activeDeckRef.current === 'A') setIsPlaying(true)
    }
    const handlePause = () => {
      if (activeDeckRef.current === 'A') setIsPlaying(false)
    }
    const handleSeeked = () => { seekingRef.current = false }
    const handleError = () => {
      if (activeDeckRef.current === 'A') {
        setIsPlaying(false)
        onErrorRef.current?.()
      }
    }

    deckA.addEventListener('loadedmetadata', handleLoadedMetadata)
    deckA.addEventListener('ended', handleEnded)
    deckA.addEventListener('play', handlePlay)
    deckA.addEventListener('pause', handlePause)
    deckA.addEventListener('seeked', handleSeeked)
    deckA.addEventListener('error', handleError)

    // --- Deck B event listeners ---

    const handleLoadedMetadataB = () => {
      if (activeDeckRef.current === 'B') {
        setDuration(deckB.duration)
      }
    }

    const handleEndedB = () => {
      if (activeDeckRef.current === 'B') {
        setIsPlaying(false)
        playbackTimeStore.set(0)
        onEndedRef.current?.()
      }
    }

    const handlePlayB = () => {
      if (activeDeckRef.current === 'B') setIsPlaying(true)
    }
    const handlePauseB = () => {
      if (activeDeckRef.current === 'B') setIsPlaying(false)
    }
    const handleSeekedB = () => { seekingRef.current = false }
    const handleErrorB = () => {
      if (activeDeckRef.current === 'B') {
        setIsPlaying(false)
        onErrorRef.current?.()
      }
    }

    deckB.addEventListener('loadedmetadata', handleLoadedMetadataB)
    deckB.addEventListener('ended', handleEndedB)
    deckB.addEventListener('play', handlePlayB)
    deckB.addEventListener('pause', handlePauseB)
    deckB.addEventListener('seeked', handleSeekedB)
    deckB.addEventListener('error', handleErrorB)

    return () => {
      deckA.removeEventListener('loadedmetadata', handleLoadedMetadata)
      deckA.removeEventListener('ended', handleEnded)
      deckA.removeEventListener('play', handlePlay)
      deckA.removeEventListener('pause', handlePause)
      deckA.removeEventListener('seeked', handleSeeked)
      deckA.removeEventListener('error', handleError)
      deckA.pause()
      deckA.src = ''

      deckB.removeEventListener('loadedmetadata', handleLoadedMetadataB)
      deckB.removeEventListener('ended', handleEndedB)
      deckB.removeEventListener('play', handlePlayB)
      deckB.removeEventListener('pause', handlePauseB)
      deckB.removeEventListener('seeked', handleSeekedB)
      deckB.removeEventListener('error', handleErrorB)
      deckB.pause()
      deckB.src = ''

      if (crossfadeTimerRef.current) clearTimeout(crossfadeTimerRef.current)
      if (seekTimeoutRef.current) clearTimeout(seekTimeoutRef.current)
    }
  }, [])

  // Update currentTime via native timeupdate event from the active deck
  useEffect(() => {
    const deckA = deckARef.current
    const deckB = deckBRef.current
    if (!deckA || !deckB || !isPlaying) return

    const handleTimeUpdate = () => {
      const active = activeDeckRef.current === 'A' ? deckA : deckB
      if (!seekingRef.current) {
        playbackTimeStore.set(active.currentTime)
      }
      // Pre-fire "about to end" callback before track finishes
      if (!aboutToEndFiredRef.current && active.duration > 0) {
        const remaining = active.duration - active.currentTime
        if (remaining > 0 && remaining <= aboutToEndThresholdRef.current) {
          aboutToEndFiredRef.current = true
          onAboutToEndRef.current?.()
        }
      }
    }

    deckA.addEventListener('timeupdate', handleTimeUpdate)
    deckB.addEventListener('timeupdate', handleTimeUpdate)
    return () => {
      deckA.removeEventListener('timeupdate', handleTimeUpdate)
      deckB.removeEventListener('timeupdate', handleTimeUpdate)
    }
  }, [isPlaying])

  /**
   * Play a track on the active deck (used for manual skips / direct play).
   * If a crossfade is in progress, snap it to completion first.
   */
  const play = useCallback((filePath: string) => {
    // Snap any in-progress crossfade
    if (crossfadingRef.current) {
      finalizeCrossfade()
    }

    const audio = activeDeckRef.current === 'A' ? deckARef.current : deckBRef.current
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
      if (err.name === 'NotAllowedError') return
      console.warn('[audio] playback failed:', err.name, err.message)
    })
  }, [finalizeCrossfade])

  // Load a track without playing (for restoring state on startup)
  const load = useCallback((filePath: string, seekTo: number = 0) => {
    const audio = deckARef.current // Always load on deck A at startup
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

  /**
   * Crossfade to a new track on the inactive deck.
   * Returns true if crossfade was initiated, false if it fell back to instant play.
   */
  const crossfadeToNext = useCallback((filePath: string, crossfadeDuration: number): boolean => {
    // Snap any existing crossfade
    if (crossfadingRef.current) {
      finalizeCrossfade()
    }

    const outgoingDeck = getActiveDeck()
    const incomingDeck = getInactiveDeck()
    const outgoingId = activeDeckRef.current
    const incomingId = getInactiveDeckId()

    if (!outgoingDeck || !incomingDeck || !filePath) return false

    // Guard: if track is too short for crossfade, skip crossfade
    const trackDuration = outgoingDeck.duration
    const effectiveDuration = Math.min(crossfadeDuration, trackDuration - 0.5)
    if (effectiveDuration < 0.5) return false

    crossfadingRef.current = true

    // Load and play the incoming track
    aboutToEndFiredRef.current = false
    const src = `audio://track/${encodeURIComponent(filePath)}`
    incomingDeck.src = src
    incomingDeck.play().catch((err: DOMException) => {
      if (err.name === 'NotAllowedError') return
      console.warn('[audio] crossfade playback failed:', err.name, err.message)
    })

    // Swap active deck immediately so state (isPlaying, duration) tracks the incoming
    activeDeckRef.current = incomingId
    audioRef.current = incomingDeck
    secondaryAudioRef.current = outgoingDeck

    // Ramp fade gains
    setDeckFadeGain(incomingId, 0) // start silent
    setDeckFadeGain(incomingId, 1, effectiveDuration) // fade in
    setDeckFadeGain(outgoingId, 1) // ensure at full
    setDeckFadeGain(outgoingId, 0, effectiveDuration) // fade out

    // After crossfade completes, clean up outgoing deck
    crossfadeTimerRef.current = setTimeout(() => {
      crossfadingRef.current = false
      crossfadeTimerRef.current = null

      // Snap gains to final values
      setDeckFadeGain(incomingId, 1)
      setDeckFadeGain(outgoingId, 0)

      // Pause and unload outgoing deck
      outgoingDeck.pause()
      outgoingDeck.src = ''
    }, effectiveDuration * 1000)

    return true
  }, [finalizeCrossfade, getActiveDeck, getInactiveDeck, getInactiveDeckId])

  const pause = useCallback(() => {
    if (crossfadingRef.current) {
      finalizeCrossfade()
    }
    const active = activeDeckRef.current === 'A' ? deckARef.current : deckBRef.current
    active?.pause()
  }, [finalizeCrossfade])

  const resume = useCallback(() => {
    const active = activeDeckRef.current === 'A' ? deckARef.current : deckBRef.current
    active?.play().catch((err) => {
      if (import.meta.env.DEV) console.warn('resume() blocked:', err)
    })
  }, [])

  const seek = useCallback((seconds: number) => {
    if (crossfadingRef.current) {
      finalizeCrossfade()
    }
    const audio = activeDeckRef.current === 'A' ? deckARef.current : deckBRef.current
    if (!audio || !audio.src) return
    seekingRef.current = true
    audio.currentTime = seconds
    playbackTimeStore.set(seconds)
    // Fallback: clear seeking flag after 500ms if 'seeked' event never fires
    if (seekTimeoutRef.current) clearTimeout(seekTimeoutRef.current)
    seekTimeoutRef.current = setTimeout(() => { seekingRef.current = false }, 500)
  }, [finalizeCrossfade])

  const setVolume = useCallback((vol: number) => {
    // Keep audioElement.volume at 1 so the AnalyserNode always sees
    // full-amplitude data for the visualizer. Control actual playback
    // volume via the Web Audio GainNode instead.
    if (deckARef.current) deckARef.current.volume = 1
    if (deckBRef.current) deckBRef.current.volume = 1
    setAnalyserVolume(Math.max(0, Math.min(1, vol)))
  }, [])

  return {
    play,
    load,
    pause,
    resume,
    seek,
    setVolume,
    crossfadeToNext,
    getCurrentTime: playbackTimeStore.get,
    duration,
    isPlaying,
    audioRef,
    secondaryAudioRef,
  }
}
