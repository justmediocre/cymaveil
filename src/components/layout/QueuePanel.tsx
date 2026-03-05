import { useCallback, useEffect, useMemo, useState } from 'react'
import { motion, AnimatePresence } from 'motion/react'
import { usePlayback } from '../../contexts/playback/PlaybackContext'
import { usePlaylistCtx } from '../../contexts/playlist/PlaylistContext'
import { useLibraryCtx } from '../../contexts/library/LibraryContext'
import { PlayIcon } from '../Icons'
import TrackList from '../TrackList'
import type { Track } from '../../types'

interface QueuePanelProps {
  show: boolean
}

type PanelMode =
  | { kind: 'now-playing'; tracks: Track[]; header: string }
  | { kind: 'queue'; tracks: Track[]; header: string; shuffled: boolean }
  | { kind: 'album'; tracks: Track[]; header: string }
  | { kind: 'empty' }

export default function QueuePanel({ show }: QueuePanelProps) {
  const { state, queueActive, currentTrack, currentAlbum, albumTracks, selectTrack, isPlaying, playNowPlaying, clearQueue } = usePlayback()
  const { tracks, getAlbumForTrack } = useLibraryCtx()
  const { nowPlayingList, clearNowPlaying, removeFromNowPlaying } = usePlaylistCtx()

  const isNowPlayingSource = state.queueSource === 'now-playing'
  const hasNowPlayingTracks = nowPlayingList.trackIds.length > 0

  // Track whether the user explicitly cleared — suppresses album fallback
  const [cleared, setCleared] = useState(false)

  // Reset cleared flag when a queue becomes active again
  useEffect(() => {
    if (queueActive || (isNowPlayingSource || (!queueActive && !cleared && hasNowPlayingTracks))) {
      setCleared(false)
    }
  }, [queueActive, isNowPlayingSource, cleared, hasNowPlayingTracks])

  const panelMode = useMemo((): PanelMode => {
    const showNowPlaying = isNowPlayingSource || (!queueActive && !cleared && hasNowPlayingTracks)

    if (showNowPlaying) {
      const npTracks = nowPlayingList.trackIds
        .map((id: string) => tracks.find((t) => t.id === id))
        .filter((t): t is NonNullable<typeof t> => t !== undefined)
      return {
        kind: 'now-playing',
        tracks: npTracks,
        header: `Now Playing \u00b7 ${nowPlayingList.trackIds.length} tracks`,
      }
    }

    if (queueActive) {
      const qTracks = state.playQueue
        .map((id) => tracks.find((t) => t.id === id))
        .filter((t): t is NonNullable<typeof t> => t !== undefined)
      return {
        kind: 'queue',
        tracks: qTracks,
        header: state.shuffle
          ? `Shuffle Queue \u00b7 ${state.playQueue.length} tracks`
          : `Now Playing \u00b7 ${currentAlbum?.title || ''}`,
        shuffled: state.shuffle,
      }
    }

    if (cleared) {
      return { kind: 'empty' }
    }

    if (albumTracks.length > 0) {
      return {
        kind: 'album',
        tracks: albumTracks,
        header: `Now Playing \u00b7 ${currentAlbum?.title || ''}`,
      }
    }

    return { kind: 'empty' }
  }, [isNowPlayingSource, queueActive, cleared, hasNowPlayingTracks, nowPlayingList.trackIds, state.playQueue, state.shuffle, tracks, albumTracks, currentAlbum])

  const panelTracks = panelMode.kind === 'empty' ? [] : panelMode.tracks
  const panelHeader = panelMode.kind === 'empty' ? 'Queue' : panelMode.header

  const handleQueueTrackSelect = useCallback(
    (trackIndex: number) => {
      if (panelMode.kind === 'now-playing') {
        selectTrack({ kind: 'now-playing' }, trackIndex)
      } else {
        selectTrack({ kind: 'queue' }, trackIndex)
      }
    },
    [selectTrack, panelMode.kind],
  )

  const handleRemoveTrack = useCallback(
    (trackId: string) => removeFromNowPlaying(trackId),
    [removeFromNowPlaying],
  )

  const handleClear = useCallback(() => {
    if (isNowPlayingSource) {
      clearNowPlaying()
    }
    clearQueue()
    setCleared(true)
  }, [isNowPlayingSource, clearNowPlaying, clearQueue])

  const handlePlayNowPlaying = useCallback(() => {
    playNowPlaying(0)
  }, [playNowPlaying])

  return (
    <AnimatePresence>
      {show && (
        <motion.div
          className="w-80 shrink-0 h-full border-l overflow-hidden glass flex flex-col"
          style={{
            background: 'var(--glass-bg-surface)',
            borderColor: 'var(--border-subtle)',
          }}
          initial={{ width: 0, opacity: 0 }}
          animate={{ width: 320, opacity: 1 }}
          exit={{ width: 0, opacity: 0 }}
          transition={{ duration: 0.35, ease: [0.22, 1, 0.36, 1] }}
        >
          <div style={{ width: 320 }} className="flex flex-col h-full p-4">
            <div className="flex items-center justify-between mb-4 shrink-0">
              <h2
                className="font-display text-xs font-bold tracking-wider uppercase"
                style={{ color: 'var(--text-tertiary)' }}
              >
                {panelHeader}
              </h2>
              <div className="flex items-center gap-1">
                {/* Play button when now-playing has tracks but nothing is playing from it */}
                {hasNowPlayingTracks && !isNowPlayingSource && (
                  <button
                    onClick={handlePlayNowPlaying}
                    className="flex items-center justify-center w-6 h-6 rounded-full cursor-pointer"
                    style={{ color: 'var(--accent)' }}
                    title="Play Now Playing list"
                    aria-label="Play Now Playing list"
                  >
                    <PlayIcon size={14} />
                  </button>
                )}
                {(panelTracks.length > 0) && (
                  <button
                    onClick={handleClear}
                    className="text-[10px] font-medium uppercase tracking-wider cursor-pointer px-1.5 py-0.5 rounded"
                    style={{ color: 'var(--text-tertiary)' }}
                    title="Clear queue"
                    aria-label="Clear queue"
                  >
                    Clear
                  </button>
                )}
              </div>
            </div>
            <div className="flex-1 min-h-0">
              <TrackList
                tracks={panelTracks}
                currentTrackId={currentTrack?.id}
                onTrackSelect={handleQueueTrackSelect}
                getAlbumForTrack={getAlbumForTrack}
                autoScrollToCurrent
                isPlaying={isPlaying}
                onRemoveTrack={panelMode.kind === 'now-playing' ? handleRemoveTrack : undefined}
              />
            </div>
          </div>
        </motion.div>
      )}
    </AnimatePresence>
  )
}
