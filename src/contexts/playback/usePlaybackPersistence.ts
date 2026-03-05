import { useEffect, useRef } from 'react'
import { playbackTimeStore } from '../../lib/playbackTimeStore'
import type { PlaybackState, PlaybackAction } from './playbackReducer'
import type { Track } from '../../types'

export function usePlaybackPersistence(
  state: PlaybackState,
  dispatch: React.Dispatch<PlaybackAction>,
  tracks: Track[],
  player: { load: (path: string, time?: number) => void },
) {
  const readyRef = useRef(false)
  const pendingRestoreRef = useRef(false)

  // ── Playback state restoration ──────────────────────────────────────────
  useEffect(() => {
    if (tracks.length === 0 || readyRef.current) return

    async function restore() {
      let targetIndex = 0
      let targetTime = 0
      let restoredQueue: string[] = []
      let restoredQueueIndex = -1
      let restoredShuffle = false
      let restoredRepeat: 'off' | 'all' | 'one' = 'off'
      let restoredQueueSource: 'none' | 'album' | 'playlist' | 'now-playing' = 'none'
      let restoredPlaybackActive = false

      if (window.electronAPI?.loadPlaybackState) {
        try {
          const saved = await window.electronAPI.loadPlaybackState()
          if (saved.currentTrackIndex >= 0 && saved.currentTrackIndex < tracks.length) {
            targetIndex = saved.currentTrackIndex
            targetTime = saved.currentTime || 0
          }
          restoredPlaybackActive = saved.playbackActive ?? (saved.currentTrackIndex >= 0)
          if (Array.isArray(saved.playQueue) && saved.playQueue.length > 0) {
            restoredQueue = saved.playQueue
            restoredQueueIndex = saved.queueIndex ?? -1
            restoredShuffle = saved.shuffle ?? false
            restoredRepeat = saved.repeat ?? 'off'
            restoredQueueSource = (saved.queueSource as typeof restoredQueueSource) ?? 'none'
          }
        } catch (err) {
          if (import.meta.env.DEV) console.warn('Failed to restore playback state:', err)
        }
      }

      // Restore queue state and playbackActive
      const restorePatch: Partial<import('./playbackReducer').PlaybackState> = {
        playbackActive: restoredPlaybackActive,
      }
      if (restoredQueue.length > 0 && restoredQueueIndex >= 0) {
        restorePatch.playQueue = restoredQueue
        restorePatch.queueIndex = restoredQueueIndex
        restorePatch.queueSource = restoredQueueSource
        restorePatch.shuffle = restoredShuffle
        restorePatch.repeat = restoredRepeat
      }
      dispatch({ type: 'RESTORE', patch: restorePatch })

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

  // ── Playback state persistence ──────────────────────────────────────────
  useEffect(() => {
    if (!readyRef.current || !window.electronAPI?.savePlaybackState) return
    window.electronAPI.savePlaybackState({
      currentTrackIndex: state.currentTrackIndex,
      currentTime: playbackTimeStore.get() || 0,
      playQueue: state.playQueue,
      queueIndex: state.queueIndex,
      queueSource: state.queueSource,
      shuffle: state.shuffle,
      repeat: state.repeat,
      playbackActive: state.playbackActive,
    })
  }, [state.currentTrackIndex, state.playQueue, state.queueIndex, state.queueSource, state.shuffle, state.repeat, state.playbackActive])

  // Push currentTime to the main process periodically
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

  return { readyRef, pendingRestoreRef }
}
