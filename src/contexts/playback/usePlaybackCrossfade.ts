import { useCallback, useMemo, useRef, useState, useSyncExternalStore } from 'react'
import useAudioPlayer from '../../hooks/useAudioPlayer'
import useAudioAnalyser from '../../hooks/useAudioAnalyser'
import { playbackSettingsStore } from '../../lib/playbackSettingsStore'
import type { PlaybackState, PlaybackAction } from './playbackReducer'
import type { Track, Album } from '../../types'

export function usePlaybackCrossfade(
  state: PlaybackState,
  dispatch: React.Dispatch<PlaybackAction>,
  tracks: Track[],
  currentTrack: Track | undefined,
  currentAlbum: Album | null,
  queueActive: boolean,
  getAlbumForTrack: (track: Track | null) => Album | null,
) {
  // Tracks whether the last track advancement was caused by crossfade (auto-advance)
  const isAutoAdvanceRef = useRef(false)

  // ── Transition intent (prefire / skip) ─────────────────────────────────
  const [transitionIntent, setTransitionIntent] = useState<'prefire' | 'skip' | null>(null)

  const clearTransitionIntent = useCallback(() => {
    setTransitionIntent(null)
  }, [])

  // Check whether the next track will be in a different album (drives prefire decision)
  const nextTrackChangesAlbum = useMemo(() => {
    if (state.repeat === 'one') return false
    let nextTrack: Track | undefined
    if (queueActive) {
      if (state.queueIndex < state.playQueue.length - 1) {
        const nextId = state.playQueue[state.queueIndex + 1]
        nextTrack = tracks.find(t => t.id === nextId)
      } else if (state.repeat === 'all') {
        const nextId = state.playQueue[0]
        nextTrack = tracks.find(t => t.id === nextId)
      }
    } else {
      const nextIndex = (state.currentTrackIndex + 1) % tracks.length
      nextTrack = tracks[nextIndex]
    }
    if (!nextTrack) return false
    const nextAlbum = getAlbumForTrack(nextTrack)
    return nextAlbum?.id !== currentAlbum?.id
  }, [state.repeat, queueActive, state.queueIndex, state.playQueue, state.currentTrackIndex, tracks, getAlbumForTrack, currentAlbum])

  /**
   * Resolve the next track without dispatching any state change.
   * Returns the Track or undefined if at end of queue with no repeat.
   */
  const resolveNextTrack = useCallback((): Track | undefined => {
    if (state.repeat === 'one') return currentTrack
    if (queueActive) {
      if (state.queueIndex < state.playQueue.length - 1) {
        const nextId = state.playQueue[state.queueIndex + 1]
        return tracks.find(t => t.id === nextId)
      } else if (state.repeat === 'all') {
        const nextId = state.playQueue[0]
        return tracks.find(t => t.id === nextId)
      }
      return undefined // queue exhausted
    }
    const nextIndex = (state.currentTrackIndex + 1) % tracks.length
    return tracks[nextIndex]
  }, [state.repeat, queueActive, state.queueIndex, state.playQueue, state.currentTrackIndex, tracks, currentTrack])

  // Dynamic aboutToEnd threshold: max(crossfadeDuration, 1.5)
  const playbackSettings = useSyncExternalStore(playbackSettingsStore.subscribe, playbackSettingsStore.get)
  const aboutToEndThreshold = Math.max(playbackSettings.crossfadeDuration, 1.5)

  // Ref to break circular dependency: handleAboutToEnd needs player.crossfadeToNext,
  // but player needs handleAboutToEnd callback.
  const crossfadeToNextRef = useRef<(filePath: string, duration: number) => boolean>(() => false)

  const handleAboutToEnd = useCallback(() => {
    if (state.repeat === 'one') return

    const cfDuration = playbackSettingsStore.get().crossfadeDuration

    if (cfDuration > 0) {
      // Crossfade: resolve next track, initiate crossfade, dispatch NEXT
      const nextTrack = resolveNextTrack()
      if (!nextTrack?.filePath) return

      const started = crossfadeToNextRef.current(nextTrack.filePath, cfDuration)
      if (started) {
        isAutoAdvanceRef.current = true
        // Set transition intent based on album change
        if (nextTrackChangesAlbum) {
          setTransitionIntent('prefire')
        }
        dispatch({ type: 'NEXT', tracksLength: tracks.length })
        return
      }
      // Crossfade didn't start (track too short) — fall through to normal prefire
    }

    if (nextTrackChangesAlbum) {
      setTransitionIntent('prefire')
    }
  }, [state.repeat, nextTrackChangesAlbum, resolveNextTrack, tracks.length])

  // Ref to break circular dependency: handleNext needs player, player needs handleNext (onEnded)
  const handleNextRef = useRef<() => void>(() => {})
  const playerRef = useRef<{ seek: (t: number) => void; resume: () => void } | null>(null)

  // Consecutive error tracking — stop advancing after too many failures in a row
  const consecutiveErrorsRef = useRef(0)
  const MAX_CONSECUTIVE_ERRORS = 3

  // Audio player hook
  const player = useAudioPlayer({
    onEnded: () => {
      consecutiveErrorsRef.current = 0
      // If crossfade already advanced the track, skip onEnded handling
      if (isAutoAdvanceRef.current) {
        isAutoAdvanceRef.current = false
        return
      }
      handleNextRef.current()
    },
    onAboutToEnd: handleAboutToEnd,
    onError: () => {
      consecutiveErrorsRef.current++
      if (consecutiveErrorsRef.current >= MAX_CONSECUTIVE_ERRORS) {
        console.warn(`[playback] ${MAX_CONSECUTIVE_ERRORS} consecutive errors, stopping`)
        consecutiveErrorsRef.current = 0
        return
      }
      handleNextRef.current()
    },
    aboutToEndThreshold,
  })
  playerRef.current = player
  crossfadeToNextRef.current = player.crossfadeToNext
  const { audioRef, secondaryAudioRef } = player

  // Audio analyser (Web Audio API)
  const { analyserRef, dataArrayRef } = useAudioAnalyser(audioRef, secondaryAudioRef, player.isPlaying)

  // Shared ref for bass energy
  const bassEnergyRef = useRef(0)

  return {
    player,
    isAutoAdvanceRef,
    transitionIntent,
    setTransitionIntent,
    clearTransitionIntent,
    handleNextRef,
    playerRef,
    consecutiveErrorsRef,
    analyserRef,
    dataArrayRef,
    bassEnergyRef,
  }
}
