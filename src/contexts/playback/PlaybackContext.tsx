import { createContext, useContext, useReducer, useCallback, useMemo, useRef, useEffect, useState, useSyncExternalStore } from 'react'
import { useLibraryCtx } from '../library/LibraryContext'
import { usePlaylistCtx } from '../playlist/PlaylistContext'
import { playbackReducer, initialPlaybackState, shuffleArray } from './playbackReducer'
import type { PlaybackState, PlaybackAction } from './playbackReducer'
import useAudioPlayer from '../../hooks/useAudioPlayer'
import useAudioAnalyser from '../../hooks/useAudioAnalyser'
import useMediaSession from '../../hooks/useMediaSession'
import { playbackTimeStore } from '../../lib/playbackTimeStore'
import { playbackSettingsStore } from '../../lib/playbackSettingsStore'
import type { Track, Album, Playlist } from '../../types'

// ── Types ────────────────────────────────────────────────────────────────────

export type TrackSource =
  | { kind: 'queue' }
  | { kind: 'global'; trackList: Track[] }
  | { kind: 'album'; albumTracks: Track[] }
  | { kind: 'playlist'; playlistId: string }

export interface UpNextTrack {
  id: string
  title: string
  artist: string
  art: string | null
}

export interface PlaybackContextValue {
  state: PlaybackState
  dispatch: React.Dispatch<PlaybackAction>

  // Derived
  currentTrack: Track | undefined
  currentAlbum: Album | null
  albumTracks: Track[]
  queueActive: boolean
  upNextTracks: UpNextTrack[]

  // Audio
  isPlaying: boolean
  duration: number
  audioRef: React.RefObject<HTMLAudioElement | null>
  analyserRef: React.RefObject<AnalyserNode | null>
  dataArrayRef: React.RefObject<Uint8Array | null>
  bassEnergyRef: React.MutableRefObject<number>

  // Transition
  transitionIntent: 'prefire' | 'skip' | null
  clearTransitionIntent: () => void

  // Actions
  handleNext: () => void
  handlePrev: () => void
  handlePlayPause: () => void
  handleShuffleToggle: () => void
  handleRepeatToggle: () => void
  setVolume: (v: number) => void
  seek: (seconds: number) => void
  selectTrack: (source: TrackSource, index: number) => void
  shuffleAll: (trackList: Track[]) => void
}

// ── Context ──────────────────────────────────────────────────────────────────

const PlaybackContext = createContext<PlaybackContextValue | null>(null)

// ── Provider ─────────────────────────────────────────────────────────────────

export function PlaybackProvider({ children }: { children: React.ReactNode }) {
  const { tracks, getAlbumForTrack, getTracksForAlbum } = useLibraryCtx()
  const { playlists } = usePlaylistCtx()

  const [state, dispatch] = useReducer(playbackReducer, initialPlaybackState)

  const queueActive = state.queueIndex >= 0 && state.playQueue.length > 0

  // Derive current track: queue takes priority
  const currentTrack = useMemo(() => {
    if (queueActive) {
      const trackId = state.playQueue[state.queueIndex]
      return tracks.find((t) => t.id === trackId) ?? undefined
    }
    return tracks[state.currentTrackIndex]
  }, [queueActive, state.playQueue, state.queueIndex, tracks, state.currentTrackIndex])

  const currentAlbum = useMemo(
    () => getAlbumForTrack(currentTrack ?? null),
    [getAlbumForTrack, currentTrack]
  )
  const albumTracks = useMemo(
    () => currentAlbum ? getTracksForAlbum(currentAlbum.id) : [],
    [currentAlbum, getTracksForAlbum]
  )

  // Compute "Up Next" tracks
  const upNextTracks = useMemo(() => {
    const nextTracks: UpNextTrack[] = []
    if (queueActive) {
      for (let i = 1; i <= 2; i++) {
        const idx = state.queueIndex + i
        if (idx < state.playQueue.length) {
          const track = tracks.find((t) => t.id === state.playQueue[idx])
          if (track) {
            const album = getAlbumForTrack(track)
            nextTracks.push({
              id: track.id,
              title: track.title,
              artist: album?.artist || 'Unknown Artist',
              art: album?.art || null,
            })
          }
        }
      }
    } else {
      for (let i = 1; i <= 2; i++) {
        const idx = (state.currentTrackIndex + i) % tracks.length
        const track = tracks[idx]
        if (track) {
          const album = getAlbumForTrack(track)
          nextTracks.push({
            id: track.id,
            title: track.title,
            artist: album?.artist || 'Unknown Artist',
            art: album?.art || null,
          })
        }
      }
    }
    return nextTracks
  }, [queueActive, state.queueIndex, state.playQueue, state.currentTrackIndex, tracks, getAlbumForTrack])

  // ── Crossfade state ───────────────────────────────────────────────────

  // Tracks whether the last track advancement was caused by crossfade (auto-advance)
  // so we can skip the normal onEnded and autoplay logic.
  const isAutoAdvanceRef = useRef(false)

  // ── Transition intent (prefire / skip) ─────────────────────────────────
  const [transitionIntent, setTransitionIntent] = useState<'prefire' | 'skip' | null>(null)

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

  const clearTransitionIntent = useCallback(() => {
    setTransitionIntent(null)
  }, [])

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
  const { duration, isPlaying, audioRef, secondaryAudioRef } = player

  // Audio analyser (Web Audio API)
  const { analyserRef, dataArrayRef } = useAudioAnalyser(audioRef, secondaryAudioRef, isPlaying)

  // Shared ref for bass energy
  const bassEnergyRef = useRef(0)

  // ── Playback state restoration ──────────────────────────────────────────

  const readyRef = useRef(false)
  const pendingRestoreRef = useRef(false)

  useEffect(() => {
    if (tracks.length === 0 || readyRef.current) return

    async function restore() {
      let targetIndex = 0
      let targetTime = 0
      let restoredQueue: string[] = []
      let restoredQueueIndex = -1
      let restoredShuffle = false

      if (window.electronAPI?.loadPlaybackState) {
        try {
          const saved = await window.electronAPI.loadPlaybackState()
          if (saved.currentTrackIndex >= 0 && saved.currentTrackIndex < tracks.length) {
            targetIndex = saved.currentTrackIndex
            targetTime = saved.currentTime || 0
          }
          if (Array.isArray(saved.playQueue) && saved.playQueue.length > 0) {
            restoredQueue = saved.playQueue
            restoredQueueIndex = saved.queueIndex ?? -1
            restoredShuffle = saved.shuffle ?? false
          }
        } catch (err) {
          if (import.meta.env.DEV) console.warn('Failed to restore playback state:', err)
        }
      }

      // Restore queue state
      if (restoredQueue.length > 0 && restoredQueueIndex >= 0) {
        dispatch({
          type: 'RESTORE',
          patch: {
            playQueue: restoredQueue,
            queueIndex: restoredQueueIndex,
            shuffle: restoredShuffle,
          },
        })
      }

      // Determine which track to load — queue takes priority
      let trackToLoad: Track | undefined
      if (restoredQueue.length > 0 && restoredQueueIndex >= 0) {
        const trackId = restoredQueue[restoredQueueIndex]
        trackToLoad = tracks.find((t) => t.id === trackId)
      }
      if (!trackToLoad) {
        trackToLoad = tracks[targetIndex]
      }

      if (trackToLoad?.filePath) {
        player.load(trackToLoad.filePath, targetTime)
      }

      if (targetIndex !== 0) {
        pendingRestoreRef.current = true
        dispatch({ type: 'SET_TRACK_INDEX', index: targetIndex })
      }

      readyRef.current = true
      dispatch({ type: 'SET_READY' })
    }
    restore()
  }, [tracks])

  // ── Track-change autoplay ───────────────────────────────────────────────

  const currentTrackId = currentTrack?.id
  const prevTrackIdRef = useRef<string | null>(null)

  useEffect(() => {
    if (!readyRef.current) return
    if (pendingRestoreRef.current) {
      pendingRestoreRef.current = false
      prevTrackIdRef.current = currentTrackId ?? null
      return
    }
    if (currentTrackId === prevTrackIdRef.current) return
    prevTrackIdRef.current = currentTrackId ?? null

    // If crossfade already started playback on the incoming deck, skip play()
    if (isAutoAdvanceRef.current) {
      // Don't clear the flag here — onEnded will clear it
      return
    }

    if (currentTrack?.filePath) {
      player.play(currentTrack.filePath)
    }
  }, [currentTrackId, currentTrack])

  // ── Volume sync/persist ─────────────────────────────────────────────────

  useEffect(() => {
    player.setVolume(state.volume / 100)
    localStorage.setItem('volume', String(state.volume))
  }, [state.volume])

  // ── Playback state persistence ──────────────────────────────────────────

  useEffect(() => {
    if (!readyRef.current || !window.electronAPI?.savePlaybackState) return
    window.electronAPI.savePlaybackState({
      currentTrackIndex: state.currentTrackIndex,
      currentTime: playbackTimeStore.get() || 0,
      playQueue: state.playQueue,
      queueIndex: state.queueIndex,
      shuffle: state.shuffle,
    })
  }, [state.currentTrackIndex, state.playQueue, state.queueIndex, state.shuffle])

  // Push currentTime to the main process periodically so it can
  // persist the latest position on close (destroy() skips beforeunload)
  useEffect(() => {
    if (!window.electronAPI?.pushPlaybackTime) return
    const id = setInterval(() => {
      const t = playbackTimeStore.get()
      if (t > 0) window.electronAPI!.pushPlaybackTime(t)
    }, 5_000)
    return () => clearInterval(id)
  }, [])

  // ── Track index clamping ────────────────────────────────────────────────

  const tracksLength = tracks.length
  useEffect(() => {
    dispatch({ type: 'CLAMP_INDEX', tracksLength })
  }, [tracksLength])

  // ── Handlers ────────────────────────────────────────────────────────────

  const handleNext = useCallback(() => {
    consecutiveErrorsRef.current = 0
    isAutoAdvanceRef.current = false // manual skip
    if (state.repeat === 'one') {
      playerRef.current?.seek(0)
      playerRef.current?.resume()
      return
    }
    // If prefire already set the intent, keep it; otherwise this is a manual skip
    setTransitionIntent(prev => prev === 'prefire' ? prev : 'skip')
    dispatch({ type: 'NEXT', tracksLength: tracks.length })
  }, [state.repeat, tracks.length])

  // Keep ref current for onEnded callback
  handleNextRef.current = handleNext

  const handlePrev = useCallback(() => {
    consecutiveErrorsRef.current = 0
    isAutoAdvanceRef.current = false // manual skip
    if (playbackTimeStore.get() > 3) {
      player.seek(0)
    } else {
      setTransitionIntent('skip')
      dispatch({ type: 'PREV', tracksLength: tracks.length })
    }
  }, [tracks.length, player])

  const handlePlayPause = useCallback(() => {
    if (isPlaying) {
      player.pause()
    } else {
      player.resume()
    }
  }, [isPlaying, player])

  const handleShuffleToggle = useCallback(() => {
    dispatch({
      type: 'TOGGLE_SHUFFLE',
      albumTrackIds: albumTracks.map((t) => t.id),
      currentTrackId: currentTrack?.id,
      tracks,
    })
  }, [albumTracks, currentTrack, tracks])

  const handleRepeatToggle = useCallback(() => {
    dispatch({ type: 'CYCLE_REPEAT' })
  }, [])

  const setVolume = useCallback((v: number) => {
    dispatch({ type: 'SET_VOLUME', volume: v })
  }, [])

  // ── Unified track selection ─────────────────────────────────────────────

  const selectTrack = useCallback(
    (source: TrackSource, index: number) => {
      isAutoAdvanceRef.current = false // manual selection
      switch (source.kind) {
        case 'queue':
          if (queueActive) {
            dispatch({ type: 'SET_QUEUE_INDEX', index })
          } else {
            // Fallback: treat as album track
            const albumTrack = albumTracks[index]
            if (albumTrack) {
              dispatch({ type: 'DEACTIVATE_QUEUE', trackId: albumTrack.id, tracks })
            }
          }
          break
        case 'global':
          {
            const track = source.trackList[index]
            if (track) dispatch({ type: 'DEACTIVATE_QUEUE', trackId: track.id, tracks })
          }
          break
        case 'album':
          {
            const track = source.albumTracks[index]
            if (track) dispatch({ type: 'DEACTIVATE_QUEUE', trackId: track.id, tracks })
          }
          break
        case 'playlist':
          {
            const playlist = playlists.find((p: Playlist) => p.id === source.playlistId)
            if (!playlist) return
            const validIds = playlist.trackIds.filter((id: string) => tracks.some((t) => t.id === id))
            if (validIds.length === 0) return
            dispatch({ type: 'SET_QUEUE', queue: validIds, index, shuffle: false })
          }
          break
      }
    },
    [queueActive, albumTracks, tracks, playlists]
  )

  // ── Shuffle All ─────────────────────────────────────────────────────────

  const shuffleAll = useCallback(
    (trackList: Track[]) => {
      if (!trackList || trackList.length === 0) return
      const ids = trackList.map((t) => t.id)
      const shuffled = shuffleArray(ids)
      dispatch({ type: 'SET_QUEUE', queue: shuffled, index: 0, shuffle: true })
    },
    []
  )

  // ── Media Session ───────────────────────────────────────────────────────

  const currentAlbumWithColors = useMemo(() => {
    if (!currentAlbum) return null
    return {
      ...currentAlbum,
      dominantColor: currentAlbum.dominantColor,
      accentColor: currentAlbum.accentColor,
    }
  }, [currentAlbum])

  useMediaSession({
    track: currentTrack ?? null,
    album: currentAlbumWithColors,
    isPlaying,
    duration,
    onPlay: () => player.resume(),
    onPause: () => player.pause(),
    onNext: handleNext,
    onPrev: handlePrev,
    onSeek: player.seek,
  })

  // ── Context value ───────────────────────────────────────────────────────

  const value = useMemo<PlaybackContextValue>(
    () => ({
      state,
      dispatch,
      currentTrack,
      currentAlbum,
      albumTracks,
      queueActive,
      upNextTracks,
      isPlaying,
      duration,
      audioRef,
      analyserRef,
      dataArrayRef,
      bassEnergyRef,
      transitionIntent,
      clearTransitionIntent,
      handleNext,
      handlePrev,
      handlePlayPause,
      handleShuffleToggle,
      handleRepeatToggle,
      setVolume,
      seek: player.seek,
      selectTrack,
      shuffleAll,
    }),
    [
      state, currentTrack, currentAlbum, albumTracks, queueActive, upNextTracks,
      isPlaying, duration, transitionIntent, handleNext, handlePrev, handlePlayPause,
      handleShuffleToggle, handleRepeatToggle, setVolume, player.seek,
      selectTrack, shuffleAll,
    ]
  )

  return <PlaybackContext.Provider value={value}>{children}</PlaybackContext.Provider>
}

// ── Hook ─────────────────────────────────────────────────────────────────────

export function usePlayback(): PlaybackContextValue {
  const ctx = useContext(PlaybackContext)
  if (!ctx) throw new Error('usePlayback must be inside PlaybackProvider')
  return ctx
}
