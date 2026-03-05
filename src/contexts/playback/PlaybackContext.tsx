import { createContext, useContext, useReducer, useMemo, useRef, useEffect } from 'react'
import { useLibraryCtx } from '../library/LibraryContext'
import { usePlaylistCtx } from '../playlist/PlaylistContext'
import { playbackReducer, initialPlaybackState } from './playbackReducer'
import { usePlaybackCrossfade } from './usePlaybackCrossfade'
import { usePlaybackPersistence } from './usePlaybackPersistence'
import { usePlaybackActions } from './usePlaybackActions'
import useMediaSession from '../../hooks/useMediaSession'
import type { Track, Album, Playlist } from '../../types'

// ── Types ────────────────────────────────────────────────────────────────────

export type TrackSource =
  | { kind: 'queue' }
  | { kind: 'global'; trackList: Track[] }
  | { kind: 'album'; albumTracks: Track[] }
  | { kind: 'playlist'; playlistId: string }
  | { kind: 'now-playing' }

export interface UpNextTrack {
  id: string
  title: string
  artist: string
  art: string | null
}

export interface PlaybackContextValue {
  state: import('./playbackReducer').PlaybackState
  dispatch: React.Dispatch<import('./playbackReducer').PlaybackAction>

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
  playAlbum: (albumId: string) => void
  playNowPlaying: (index?: number) => void
  addToNowPlayingAndPlay: (trackId: string) => void
  clearQueue: () => void
}

// ── Context ──────────────────────────────────────────────────────────────────

const PlaybackContext = createContext<PlaybackContextValue | null>(null)

// ── Provider ─────────────────────────────────────────────────────────────────

export function PlaybackProvider({ children }: { children: React.ReactNode }) {
  const { tracks, getAlbumForTrack, getTracksForAlbum } = useLibraryCtx()
  const { playlists, nowPlayingList, addToNowPlaying } = usePlaylistCtx()

  const [state, dispatch] = useReducer(playbackReducer, initialPlaybackState)

  const queueActive = state.queueIndex >= 0 && state.playQueue.length > 0

  // Derive current track: queue takes priority; undefined when playback cleared
  const currentTrack = useMemo(() => {
    if (!state.playbackActive) return undefined
    if (queueActive) {
      const trackId = state.playQueue[state.queueIndex]
      return tracks.find((t) => t.id === trackId) ?? undefined
    }
    return tracks[state.currentTrackIndex]
  }, [state.playbackActive, queueActive, state.playQueue, state.queueIndex, tracks, state.currentTrackIndex])

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
    if (!state.playbackActive) return []
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
              artist: track.artist || 'Unknown Artist',
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
            artist: track.artist || 'Unknown Artist',
            art: album?.art || null,
          })
        }
      }
    }
    return nextTracks
  }, [state.playbackActive, queueActive, state.queueIndex, state.playQueue, state.currentTrackIndex, tracks, getAlbumForTrack])

  // ── Composed hooks ─────────────────────────────────────────────────────

  const crossfade = usePlaybackCrossfade(
    state, dispatch, tracks, currentTrack, currentAlbum, queueActive, getAlbumForTrack,
  )

  const { player, isAutoAdvanceRef, transitionIntent, clearTransitionIntent,
    analyserRef, dataArrayRef, bassEnergyRef } = crossfade
  const { duration, isPlaying, audioRef } = player

  const { readyRef, pendingRestoreRef } = usePlaybackPersistence(
    state, dispatch, tracks, player,
  )

  const actions = usePlaybackActions(
    state, dispatch, tracks, currentTrack, albumTracks, queueActive,
    player, crossfade, playlists, nowPlayingList, addToNowPlaying, getTracksForAlbum,
  )

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

    // If queue was just cleared, suppress autoplay on the fallback track
    if (state.suppressAutoplay) {
      dispatch({ type: 'CLEAR_SUPPRESS_AUTOPLAY' })
      return
    }

    // If crossfade already started playback on the incoming deck, skip play()
    if (isAutoAdvanceRef.current) {
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

  // ── Media Session ───────────────────────────────────────────────────────

  useMediaSession({
    track: currentTrack ?? null,
    album: currentAlbum ?? null,
    isPlaying,
    duration,
    onPlay: () => player.resume(),
    onPause: () => player.pause(),
    onNext: actions.handleNext,
    onPrev: actions.handlePrev,
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
      handleNext: actions.handleNext,
      handlePrev: actions.handlePrev,
      handlePlayPause: actions.handlePlayPause,
      handleShuffleToggle: actions.handleShuffleToggle,
      handleRepeatToggle: actions.handleRepeatToggle,
      setVolume: actions.setVolume,
      seek: player.seek,
      selectTrack: actions.selectTrack,
      shuffleAll: actions.shuffleAll,
      playAlbum: actions.playAlbum,
      playNowPlaying: actions.playNowPlaying,
      addToNowPlayingAndPlay: actions.addToNowPlayingAndPlay,
      clearQueue: actions.clearQueue,
    }),
    [
      state, currentTrack, currentAlbum, albumTracks, queueActive, upNextTracks,
      isPlaying, duration, transitionIntent, actions.handleNext, actions.handlePrev,
      actions.handlePlayPause, actions.handleShuffleToggle, actions.handleRepeatToggle,
      actions.setVolume, player.seek, actions.selectTrack, actions.shuffleAll,
      actions.playAlbum, actions.playNowPlaying, actions.addToNowPlayingAndPlay, actions.clearQueue,
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
