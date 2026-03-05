import { useState, useCallback, useMemo, useSyncExternalStore } from 'react'
import { motion } from 'motion/react'
import { useLibraryCtx } from '../../contexts/library/LibraryContext'
import { usePlaylistCtx } from '../../contexts/playlist/PlaylistContext'
import { usePlayback } from '../../contexts/playback/PlaybackContext'
import { ChevronLeftIcon, ShuffleIcon, HeartIcon } from '../Icons'
import TrackList from '../TrackList'
import { useClickHandler } from '../../hooks/useClickMode'
import { playbackSettingsStore } from '../../lib/playbackSettingsStore'
import { NOW_PLAYING_ID } from '../../hooks/usePlaylists'
import type { Track } from '../../types'

interface PlaylistDetailViewProps {
  playlistId: string
  onBack: () => void
  onNavigateToNowPlaying: () => void
  onDeleteNavigate?: () => void
}

export default function PlaylistDetailView({
  playlistId,
  onBack,
  onNavigateToNowPlaying,
  onDeleteNavigate,
}: PlaylistDetailViewProps) {
  const { tracks: allTracks, getAlbumForTrack } = useLibraryCtx()
  const {
    playlists, renamePlaylist, deletePlaylist, exportPlaylist,
    removeTrackFromPlaylist, isTrackFavorited, toggleFavorite,
    addTrackToPlaylist, createPlaylist, addToNowPlaying, isInNowPlaying,
  } = usePlaylistCtx()
  const { currentTrack, isPlaying, selectTrack, shuffleAll, addToNowPlayingAndPlay } = usePlayback()
  const { clickMode } = useSyncExternalStore(playbackSettingsStore.subscribe, playbackSettingsStore.get)

  const [isEditing, setIsEditing] = useState(false)
  const [editName, setEditName] = useState('')

  const playlist = playlists.find((p) => p.id === playlistId)

  // Resolve track IDs to track objects
  const playlistTracks = useMemo(() => {
    if (!playlist) return []
    return playlist.trackIds
      .map((id) => allTracks.find((t) => t.id === id))
      .filter((t): t is Track => Boolean(t))
  }, [playlist, allTracks])

  const handleClassicSelect = useCallback(
    (idx: number) => selectTrack({ kind: 'playlist', playlistId }, idx),
    [selectTrack, playlistId],
  )

  const handleQueueSingle = useCallback(
    (idx: number) => {
      const track = playlistTracks[idx]
      if (track) addToNowPlaying(track.id)
    },
    [playlistTracks, addToNowPlaying]
  )

  const handleQueueDouble = useCallback(
    (idx: number) => {
      const track = playlistTracks[idx]
      if (track) addToNowPlayingAndPlay(track.id)
    },
    [playlistTracks, addToNowPlayingAndPlay]
  )

  const clickHandler = useClickHandler(handleQueueSingle, handleQueueDouble)
  const handleTrackSelect = clickMode === 'queue-building' ? clickHandler : handleClassicSelect

  const handleRemoveTrack = useCallback(
    (trackId: string) => removeTrackFromPlaylist(playlistId, trackId),
    [removeTrackFromPlaylist, playlistId],
  )

  const handleShuffleAll = useCallback(() => {
    shuffleAll(playlistTracks)
    onNavigateToNowPlaying()
  }, [shuffleAll, playlistTracks, onNavigateToNowPlaying])

  if (!playlist) return null

  const isFavorites = playlist.id === 'favorites'
  const isProtected = isFavorites || playlist.id === NOW_PLAYING_ID

  const handleDelete = () => {
    if (confirm(`Delete "${playlist.name}"?`)) {
      deletePlaylist(playlist.id)
      onDeleteNavigate?.()
    }
  }

  const handleStartRename = () => {
    setEditName(playlist.name)
    setIsEditing(true)
  }

  const handleFinishRename = () => {
    const name = editName.trim()
    if (name && name !== playlist.name) {
      renamePlaylist(playlist.id, name)
    }
    setIsEditing(false)
  }

  const handleRenameKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter') handleFinishRename()
    if (e.key === 'Escape') setIsEditing(false)
  }

  return (
    <div className="h-full flex flex-col overflow-hidden px-10">
      {/* Back button */}
      <div className="pt-6 pb-4 shrink-0">
        <motion.button
          onClick={onBack}
          className="flex items-center gap-1 text-sm"
          style={{ color: 'var(--text-secondary)' }}
          whileHover={{ color: 'var(--text-primary)', x: -2 }}
          whileTap={{ scale: 0.97 }}
        >
          <ChevronLeftIcon size={16} />
          <span>Playlists</span>
        </motion.button>
      </div>

      {/* Playlist header */}
      <div className="flex items-start gap-4 mb-6 shrink-0">
        <div
          className="w-16 h-16 rounded-xl flex items-center justify-center shrink-0"
          style={{
            background: isFavorites ? 'var(--accent-dim)' : 'var(--bg-elevated)',
            color: isFavorites ? 'var(--accent)' : 'var(--text-tertiary)',
          }}
        >
          {isFavorites ? <HeartIcon size={28} filled /> : (
            <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
              <line x1="8" y1="6" x2="21" y2="6" />
              <line x1="8" y1="12" x2="21" y2="12" />
              <line x1="8" y1="18" x2="21" y2="18" />
              <line x1="3" y1="6" x2="3.01" y2="6" />
              <line x1="3" y1="12" x2="3.01" y2="12" />
              <line x1="3" y1="18" x2="3.01" y2="18" />
            </svg>
          )}
        </div>
        <div className="flex-1 min-w-0">
          {isEditing ? (
            <input
              autoFocus
              type="text"
              value={editName}
              onChange={(e: React.ChangeEvent<HTMLInputElement>) => setEditName(e.target.value)}
              onKeyDown={handleRenameKeyDown}
              onBlur={handleFinishRename}
              className="bg-transparent border-none font-display text-2xl font-bold w-full"
              style={{ color: 'var(--text-primary)' }}
            />
          ) : (
            <h1
              className="font-display text-2xl font-bold truncate"
              style={{ color: 'var(--text-primary)', cursor: isProtected ? 'default' : 'pointer' }}
              onClick={!isProtected ? handleStartRename : undefined}
              title={!isProtected ? 'Click to rename' : undefined}
            >
              {playlist.name}
            </h1>
          )}
          <p className="text-sm mt-1" style={{ color: 'var(--text-tertiary)' }}>
            {playlistTracks.length} {playlistTracks.length === 1 ? 'track' : 'tracks'}
          </p>
          <div className="flex items-center gap-2 mt-3">
            {playlistTracks.length > 0 && (
              <motion.button
                onClick={handleShuffleAll}
                className="no-drag flex items-center gap-1.5 px-3 py-1.5 rounded-full text-xs font-medium"
                style={{ background: 'var(--accent-dim)', color: 'var(--accent)' }}
                whileHover={{ scale: 1.05 }}
                whileTap={{ scale: 0.95 }}
              >
                <ShuffleIcon size={14} />
                Shuffle
              </motion.button>
            )}
            {playlistTracks.length > 0 && (
              <motion.button
                onClick={() => exportPlaylist(playlist, allTracks)}
                className="no-drag flex items-center gap-1.5 px-3 py-1.5 rounded-full text-xs font-medium"
                style={{ background: 'var(--bg-elevated)', color: 'var(--text-secondary)' }}
                whileHover={{ scale: 1.05 }}
                whileTap={{ scale: 0.95 }}
              >
                Export
              </motion.button>
            )}
            {!isProtected && (
              <motion.button
                onClick={handleDelete}
                className="no-drag flex items-center gap-1.5 px-3 py-1.5 rounded-full text-xs font-medium"
                style={{ background: 'var(--bg-elevated)', color: 'var(--text-secondary)' }}
                whileHover={{ scale: 1.05 }}
                whileTap={{ scale: 0.95 }}
              >
                Delete
              </motion.button>
            )}
          </div>
        </div>
      </div>

      {/* Track list */}
      <div className="flex-1 min-h-0 pb-6">
        {playlistTracks.length > 0 ? (
          <TrackList
            tracks={playlistTracks}
            currentTrackId={currentTrack?.id}
            onTrackSelect={handleTrackSelect}
            getAlbumForTrack={getAlbumForTrack}
            isPlaying={isPlaying}
            onRemoveTrack={handleRemoveTrack}
            isTrackFavorited={isTrackFavorited}
            onToggleFavorite={toggleFavorite}
            onAddToPlaylist={addTrackToPlaylist}
            onCreatePlaylist={createPlaylist}
            playlists={playlists}
            isInNowPlaying={clickMode === 'queue-building' ? isInNowPlaying : undefined}
          />
        ) : (
          <div className="flex items-center justify-center h-32">
            <p className="text-sm" style={{ color: 'var(--text-tertiary)' }}>
              {isFavorites ? 'No favorites yet — click the heart on any track' : 'No tracks — add some from your library'}
            </p>
          </div>
        )}
      </div>
    </div>
  )
}
