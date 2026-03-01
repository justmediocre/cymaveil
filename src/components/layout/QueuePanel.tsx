import { useCallback, useMemo } from 'react'
import { motion, AnimatePresence } from 'motion/react'
import { usePlayback } from '../../contexts/playback/PlaybackContext'
import { useLibraryCtx } from '../../contexts/library/LibraryContext'
import TrackList from '../TrackList'

interface QueuePanelProps {
  show: boolean
}

export default function QueuePanel({ show }: QueuePanelProps) {
  const { state, queueActive, currentTrack, currentAlbum, albumTracks, selectTrack, isPlaying } = usePlayback()
  const { tracks, getAlbumForTrack } = useLibraryCtx()

  const queuePanelTracks = useMemo(() => {
    if (queueActive) {
      return state.playQueue
        .map((id) => tracks.find((t) => t.id === id))
        .filter((t): t is NonNullable<typeof t> => t !== undefined)
    }
    return albumTracks
  }, [queueActive, state.playQueue, tracks, albumTracks])

  const queuePanelHeader = queueActive
    ? `Shuffle Queue \u00b7 ${state.playQueue.length} tracks`
    : `Now Playing \u00b7 ${currentAlbum?.title || ''}`

  const handleQueueTrackSelect = useCallback(
    (trackIndex: number) => selectTrack({ kind: 'queue' }, trackIndex),
    [selectTrack],
  )

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
            <h2
              className="font-display text-xs font-bold tracking-wider uppercase mb-4 shrink-0"
              style={{ color: 'var(--text-tertiary)' }}
            >
              {queuePanelHeader}
            </h2>
            <div className="flex-1 min-h-0">
              <TrackList
                tracks={queuePanelTracks}
                currentTrackId={currentTrack?.id}
                onTrackSelect={handleQueueTrackSelect}
                getAlbumForTrack={getAlbumForTrack}
                autoScrollToCurrent
                isPlaying={isPlaying}
              />
            </div>
          </div>
        </motion.div>
      )}
    </AnimatePresence>
  )
}
