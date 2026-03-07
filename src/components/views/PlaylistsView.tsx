import { useState, useCallback } from 'react'
import { motion } from 'motion/react'
import { useLibraryCtx } from '../../contexts/library/LibraryContext'
import { usePlaylistCtx } from '../../contexts/playlist/PlaylistContext'
import { HeartIcon, ListIcon, PlayIcon } from '../Icons'
import NewPlaylistInput from '../NewPlaylistInput'
import { NOW_PLAYING_ID } from '../../hooks/usePlaylists'

interface PlaylistsViewProps {
  onPlaylistSelect: (playlistId: string) => void
}

export default function PlaylistsView({ onPlaylistSelect }: PlaylistsViewProps) {
  const { tracks } = useLibraryCtx()
  const { playlists, createPlaylist, importPlaylist } = usePlaylistCtx()

  const [showNewInput, setShowNewInput] = useState(false)

  const handleCreate = (name: string) => {
    createPlaylist(name)
    setShowNewInput(false)
  }

  const handleImportPlaylist = useCallback(async () => {
    await importPlaylist(tracks)
  }, [importPlaylist, tracks])

  const userPlaylists = playlists.filter((p) => p.id !== 'favorites' && p.id !== NOW_PLAYING_ID)
  const favorites = playlists.find((p) => p.id === 'favorites')
  const nowPlaying = playlists.find((p) => p.id === NOW_PLAYING_ID)

  return (
    <div className="h-full overflow-y-auto overflow-x-hidden px-10 pb-6">
      <div className="flex items-center gap-3 pt-6 pb-5">
        <h1 className="font-display text-2xl font-bold" style={{ color: 'var(--text-primary)' }}>
          Playlists
        </h1>
        <span className="text-sm" style={{ color: 'var(--text-tertiary)' }}>
          {playlists.length} {playlists.length === 1 ? 'playlist' : 'playlists'}
        </span>
        <div className="ml-auto flex items-center gap-2">
          <motion.button
            onClick={handleImportPlaylist}
            className="no-drag flex items-center gap-1.5 px-3 py-1.5 rounded-full text-xs font-medium"
            style={{ background: 'var(--bg-elevated)', color: 'var(--text-secondary)' }}
            whileHover={{ scale: 1.05 }}
            whileTap={{ scale: 0.95 }}
          >
            Import .m3u8
          </motion.button>
          <motion.button
            onClick={() => setShowNewInput(true)}
            className="no-drag flex items-center gap-1.5 px-3 py-1.5 rounded-full text-xs font-medium"
            style={{ background: 'var(--accent-dim)', color: 'var(--accent)' }}
            whileHover={{ scale: 1.05 }}
            whileTap={{ scale: 0.95 }}
          >
            + New Playlist
          </motion.button>
        </div>
      </div>

      {/* New playlist inline input */}
      {showNewInput && (
        <NewPlaylistInput
          onSubmit={handleCreate}
          onCancel={() => setShowNewInput(false)}
        />
      )}

      {/* Playlist list */}
      <div className="space-y-1">
        {/* Favorites always first */}
        {favorites && (
          <motion.button
            onClick={() => onPlaylistSelect('favorites')}
            className="no-drag w-full flex items-center gap-3 px-4 py-3 rounded-xl text-left"
            style={{ background: 'transparent' }}
            whileHover={{ background: 'var(--bg-hover)' }}
            whileTap={{ scale: 0.99 }}
            transition={{ duration: 0.15, ease: 'easeOut' }}
          >
            <div
              className="w-10 h-10 rounded-lg flex items-center justify-center shrink-0"
              style={{ background: 'var(--accent-dim)', color: 'var(--accent)' }}
            >
              <HeartIcon size={18} filled />
            </div>
            <div className="flex-1 min-w-0">
              <p className="text-sm font-medium truncate" style={{ color: 'var(--text-primary)' }}>
                Favorites
              </p>
              <p className="text-xs" style={{ color: 'var(--text-tertiary)' }}>
                {favorites.trackIds.length} {favorites.trackIds.length === 1 ? 'track' : 'tracks'}
              </p>
            </div>
          </motion.button>
        )}

        {/* Now Playing */}
        {nowPlaying && (
          <motion.button
            onClick={() => onPlaylistSelect(NOW_PLAYING_ID)}
            className="no-drag w-full flex items-center gap-3 px-4 py-3 rounded-xl text-left"
            style={{ background: 'transparent' }}
            whileHover={{ background: 'var(--bg-hover)' }}
            whileTap={{ scale: 0.99 }}
            transition={{ duration: 0.15, ease: 'easeOut' }}
          >
            <div
              className="w-10 h-10 rounded-lg flex items-center justify-center shrink-0"
              style={{ background: 'var(--accent-dim)', color: 'var(--accent)' }}
            >
              <PlayIcon size={18} />
            </div>
            <div className="flex-1 min-w-0">
              <p className="text-sm font-medium truncate" style={{ color: 'var(--text-primary)' }}>
                Now Playing
              </p>
              <p className="text-xs" style={{ color: 'var(--text-tertiary)' }}>
                {nowPlaying.trackIds.length} {nowPlaying.trackIds.length === 1 ? 'track' : 'tracks'}
              </p>
            </div>
          </motion.button>
        )}

        {/* User playlists */}
        {userPlaylists.map((playlist) => (
          <motion.button
            key={playlist.id}
            onClick={() => onPlaylistSelect(playlist.id)}
            className="no-drag w-full flex items-center gap-3 px-4 py-3 rounded-xl text-left"
            style={{ background: 'transparent' }}
            whileHover={{ background: 'var(--bg-hover)' }}
            whileTap={{ scale: 0.99 }}
            transition={{ duration: 0.15, ease: 'easeOut' }}
          >
            <div
              className="w-10 h-10 rounded-lg flex items-center justify-center shrink-0"
              style={{ background: 'var(--bg-elevated)', color: 'var(--text-tertiary)' }}
            >
              <ListIcon size={18} />
            </div>
            <div className="flex-1 min-w-0">
              <p className="text-sm font-medium truncate" style={{ color: 'var(--text-primary)' }}>
                {playlist.name}
              </p>
              <p className="text-xs" style={{ color: 'var(--text-tertiary)' }}>
                {playlist.trackIds.length} {playlist.trackIds.length === 1 ? 'track' : 'tracks'}
              </p>
            </div>
          </motion.button>
        ))}

        {playlists.length <= 1 && (
          <div className="flex items-center justify-center h-32">
            <p className="text-sm" style={{ color: 'var(--text-tertiary)' }}>
              No playlists yet — create one above
            </p>
          </div>
        )}
      </div>
    </div>
  )
}
