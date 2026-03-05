import { useRef, useCallback, useSyncExternalStore } from 'react'
import { usePlaylistCtx } from '../contexts/playlist/PlaylistContext'
import { usePlayback } from '../contexts/playback/PlaybackContext'
import { playbackSettingsStore } from '../lib/playbackSettingsStore'
import type { Track } from '../types'

const DOUBLE_CLICK_DELAY = 250

/**
 * Returns a single click handler that discriminates between single and double clicks.
 * Single click fires after a 250ms delay (cancelled if a second click arrives).
 * Double click fires immediately on the second click.
 */
export function useClickHandler(
  onSingle: (index: number) => void,
  onDouble: (index: number) => void,
) {
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null)
  const pendingIndexRef = useRef<number>(-1)

  return useCallback(
    (index: number) => {
      if (timerRef.current !== null && pendingIndexRef.current === index) {
        // Second click on same item — double click
        clearTimeout(timerRef.current)
        timerRef.current = null
        onDouble(index)
      } else {
        // First click (or different item) — start timer
        if (timerRef.current !== null) {
          clearTimeout(timerRef.current)
        }
        pendingIndexRef.current = index
        timerRef.current = setTimeout(() => {
          timerRef.current = null
          onSingle(index)
        }, DOUBLE_CLICK_DELAY)
      }
    },
    [onSingle, onDouble],
  )
}

/**
 * Encapsulates click-mode wiring for track lists.
 * In classic mode, delegates to onClassicSelect.
 * In queue-building mode, single-click adds to Now Playing,
 * double-click adds and starts playback.
 */
export function useTrackClickHandler(
  tracks: Track[],
  onClassicSelect: (index: number) => void,
): { handleTrackSelect: (index: number) => void; isQueueBuilding: boolean } {
  const { addToNowPlaying, isInNowPlaying } = usePlaylistCtx()
  const { addToNowPlayingAndPlay } = usePlayback()
  const { clickMode } = useSyncExternalStore(playbackSettingsStore.subscribe, playbackSettingsStore.get)

  const handleQueueSingle = useCallback(
    (idx: number) => {
      const track = tracks[idx]
      if (track) addToNowPlaying(track.id)
    },
    [tracks, addToNowPlaying],
  )

  const handleQueueDouble = useCallback(
    (idx: number) => {
      const track = tracks[idx]
      if (track) addToNowPlayingAndPlay(track.id)
    },
    [tracks, addToNowPlayingAndPlay],
  )

  const clickHandler = useClickHandler(handleQueueSingle, handleQueueDouble)
  const isQueueBuilding = clickMode === 'queue-building'

  return {
    handleTrackSelect: isQueueBuilding ? clickHandler : onClassicSelect,
    isQueueBuilding,
  }
}
