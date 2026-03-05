import { useMemo, useCallback } from 'react'
import { motion } from 'motion/react'
import { useLibraryCtx } from '../../contexts/library/LibraryContext'
import { usePlayback } from '../../contexts/playback/PlaybackContext'
import { ShuffleIcon } from '../Icons'
import AlbumCard from '../AlbumCard'
import EmptyState from '../EmptyState'

interface AlbumsViewProps {
  onAlbumSelect: (albumId: string) => void
  onNavigateToNowPlaying: () => void
}

export default function AlbumsView({ onAlbumSelect, onNavigateToNowPlaying }: AlbumsViewProps) {
  const { albums, tracks } = useLibraryCtx()
  const { shuffleAll, playAlbum } = usePlayback()

  const sortedAlbums = useMemo(
    () => [...albums].sort((a, b) => a.title.localeCompare(b.title)),
    [albums]
  )

  const handleShuffleAll = useCallback(() => {
    shuffleAll(tracks)
    onNavigateToNowPlaying()
  }, [shuffleAll, tracks, onNavigateToNowPlaying])

  const handlePlayAlbum = useCallback(
    (albumId: string) => playAlbum(albumId),
    [playAlbum]
  )

  if (sortedAlbums.length === 0) {
    return (
      <EmptyState
        icon="disc"
        title="No albums yet"
        subtitle="Add a music folder to get started"
        showImport
      />
    )
  }

  return (
    <div className="h-full overflow-y-auto px-10 pb-6">
      <div className="flex items-center gap-3 pt-6 pb-5">
        <h1 className="font-display text-2xl font-bold" style={{ color: 'var(--text-primary)' }}>
          Albums
        </h1>
        <span className="text-sm" style={{ color: 'var(--text-tertiary)' }}>
          {sortedAlbums.length} {sortedAlbums.length === 1 ? 'album' : 'albums'}
        </span>
        {sortedAlbums.length > 0 && (
          <motion.button
            onClick={handleShuffleAll}
            className="no-drag ml-auto flex items-center gap-1.5 px-3 py-1.5 rounded-full text-xs font-medium"
            style={{
              background: 'var(--accent-dim)',
              color: 'var(--accent)',
            }}
            whileHover={{ scale: 1.05 }}
            whileTap={{ scale: 0.95 }}
          >
            <ShuffleIcon size={14} />
            Shuffle All
          </motion.button>
        )}
      </div>
      <div
        className="grid gap-4"
        style={{ gridTemplateColumns: 'repeat(auto-fill, minmax(160px, 1fr))' }}
      >
        {sortedAlbums.map((album) => (
          <AlbumCard key={album.id} album={album} onClick={onAlbumSelect} onPlayAlbum={handlePlayAlbum} />
        ))}
      </div>
    </div>
  )
}
