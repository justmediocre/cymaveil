import { useCallback, useEffect, useRef } from 'react'
import { playbackTimeStore } from '../../lib/playbackTimeStore'
import { shuffleArray } from './playbackReducer'
import type { PlaybackState, PlaybackAction } from './playbackReducer'
import type { TrackSource } from './PlaybackContext'
import type { Track, Playlist } from '../../types'
import type useAudioPlayer from '../../hooks/useAudioPlayer'

interface CrossfadeRefs {
  isAutoAdvanceRef: React.MutableRefObject<boolean>
  setTransitionIntent: (v: 'prefire' | 'skip' | null | ((prev: 'prefire' | 'skip' | null) => 'prefire' | 'skip' | null)) => void
  consecutiveErrorsRef: React.MutableRefObject<number>
  playerRef: React.MutableRefObject<{ seek: (t: number) => void; resume: () => void } | null>
  handleNextRef: React.MutableRefObject<() => void>
}

export function usePlaybackActions(
  state: PlaybackState,
  dispatch: React.Dispatch<PlaybackAction>,
  tracks: Track[],
  currentTrack: Track | undefined,
  albumTracks: Track[],
  queueActive: boolean,
  player: ReturnType<typeof useAudioPlayer>,
  crossfade: CrossfadeRefs,
  playlists: Playlist[],
  nowPlayingList: { trackIds: string[] },
  addToNowPlaying: (id: string) => void,
  getTracksForAlbum: (albumId: string) => Track[],
) {
  const { isAutoAdvanceRef, setTransitionIntent, consecutiveErrorsRef, playerRef, handleNextRef } = crossfade

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
    if (player.isPlaying) {
      player.pause()
    } else {
      player.resume()
    }
  }, [player.isPlaying, player])

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
              const fallback = tracks.findIndex((t) => t.id === albumTrack.id)
              dispatch({ type: 'EXIT_QUEUE', fallbackTrackIndex: fallback >= 0 ? fallback : state.currentTrackIndex })
            }
          }
          break
        case 'global':
          {
            const track = source.trackList[index]
            if (track) {
              const fallback = tracks.findIndex((t) => t.id === track.id)
              dispatch({ type: 'EXIT_QUEUE', fallbackTrackIndex: fallback >= 0 ? fallback : state.currentTrackIndex })
            }
          }
          break
        case 'album':
          {
            const trackIds = source.albumTracks.map(t => t.id)
            dispatch({ type: 'SET_QUEUE', queue: trackIds, index, shuffle: false, source: 'album' })
          }
          break
        case 'playlist':
          {
            const playlist = playlists.find((p: Playlist) => p.id === source.playlistId)
            if (!playlist) return
            const validIds = playlist.trackIds.filter((id: string) => tracks.some((t) => t.id === id))
            if (validIds.length === 0) return
            dispatch({ type: 'SET_QUEUE', queue: validIds, index, shuffle: false, source: 'playlist' })
          }
          break
        case 'now-playing':
          {
            const npList = nowPlayingList.trackIds
            if (npList.length === 0) return
            dispatch({ type: 'SET_QUEUE', queue: npList, index, shuffle: false, source: 'now-playing' })
          }
          break
      }
    },
    [queueActive, albumTracks, tracks, playlists, nowPlayingList, state.currentTrackIndex]
  )

  // ── Shuffle All ─────────────────────────────────────────────────────────

  const shuffleAll = useCallback(
    (trackList: Track[]) => {
      if (!trackList || trackList.length === 0) return
      const ids = trackList.map((t) => t.id)
      const shuffled = shuffleArray(ids)
      dispatch({ type: 'SET_QUEUE', queue: shuffled, index: 0, shuffle: true, source: 'none' })
    },
    []
  )

  // ── Play Album ────────────────────────────────────────────────────────

  const playAlbum = useCallback(
    (albumId: string) => {
      const albumTrackList = getTracksForAlbum(albumId)
      if (albumTrackList.length === 0) return
      const ids = albumTrackList.map((t) => t.id)
      dispatch({ type: 'SET_QUEUE', queue: ids, index: 0, shuffle: false, source: 'album' })
    },
    [getTracksForAlbum]
  )

  // ── Play Now Playing ─────────────────────────────────────────────────

  const playNowPlaying = useCallback(
    (index?: number) => {
      const ids = nowPlayingList.trackIds
      if (ids.length === 0) return
      dispatch({ type: 'SET_QUEUE', queue: ids, index: index ?? 0, shuffle: false, source: 'now-playing' })
    },
    [nowPlayingList]
  )

  // ── Add to Now Playing and Play ──────────────────────────────────────

  const addToNowPlayingAndPlay = useCallback(
    (trackId: string) => {
      addToNowPlaying(trackId)
      const currentIds = nowPlayingList.trackIds
      const newIds = currentIds.includes(trackId) ? currentIds : [...currentIds, trackId]
      const idx = newIds.indexOf(trackId)
      dispatch({ type: 'SET_QUEUE', queue: newIds, index: idx, shuffle: false, source: 'now-playing' })
    },
    [addToNowPlaying, nowPlayingList]
  )

  // ── Clear Queue (stop playback + deactivate) ─────────────────────────

  const clearQueue = useCallback(() => {
    player.pause()
    dispatch({ type: 'STOP_PLAYBACK' })
  }, [player])

  // ── Sync now-playing list → playQueue when source is 'now-playing' ──

  const prevNowPlayingIdsRef = useRef<string[]>([])
  useEffect(() => {
    if (state.queueSource !== 'now-playing') {
      prevNowPlayingIdsRef.current = []
      return
    }
    const newIds = nowPlayingList.trackIds
    const prevIds = prevNowPlayingIdsRef.current
    // Skip if unchanged
    if (newIds.length === prevIds.length && newIds.every((id: string, i: number) => id === prevIds[i])) return
    prevNowPlayingIdsRef.current = newIds

    if (newIds.length === 0) {
      dispatch({ type: 'EXIT_QUEUE', fallbackTrackIndex: state.currentTrackIndex })
      return
    }

    // Find current track ID to preserve position
    const currentId = state.playQueue[state.queueIndex]
    const newIndex = currentId ? newIds.indexOf(currentId) : -1
    dispatch({
      type: 'SET_QUEUE',
      queue: newIds,
      index: newIndex >= 0 ? newIndex : Math.min(state.queueIndex, newIds.length - 1),
      source: 'now-playing',
    })
  }, [nowPlayingList.trackIds, state.queueSource])

  return {
    handleNext,
    handlePrev,
    handlePlayPause,
    handleShuffleToggle,
    handleRepeatToggle,
    setVolume,
    selectTrack,
    shuffleAll,
    playAlbum,
    playNowPlaying,
    addToNowPlayingAndPlay,
    clearQueue,
  }
}
