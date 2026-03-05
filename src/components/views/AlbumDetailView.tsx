import { useCallback, useMemo, useSyncExternalStore } from 'react'
import { motion } from 'motion/react'
import { useLibraryCtx } from '../../contexts/library/LibraryContext'
import { usePlaylistCtx } from '../../contexts/playlist/PlaylistContext'
import { usePlayback } from '../../contexts/playback/PlaybackContext'
import { ChevronLeftIcon, ShuffleIcon, PlayIcon } from '../Icons'
import TrackList from '../TrackList'
import { useClickHandler } from '../../hooks/useClickMode'
import { playbackSettingsStore } from '../../lib/playbackSettingsStore'
import type { Album } from '../../types'

interface AlbumDetailViewProps {
  albumId: string | null
  onBack: () => void
  onNavigateToNowPlaying: () => void
}

export default function AlbumDetailView({ albumId, onBack, onNavigateToNowPlaying }: AlbumDetailViewProps) {
  const { albums, getAlbumForTrack, getTracksForAlbum } = useLibraryCtx()
  const { isTrackFavorited, toggleFavorite, addTrackToPlaylist, createPlaylist, playlists, addToNowPlaying, isInNowPlaying } = usePlaylistCtx()
  const { currentTrack, isPlaying, selectTrack, shuffleAll, playAlbum, addToNowPlayingAndPlay } = usePlayback()
  const { clickMode } = useSyncExternalStore(playbackSettingsStore.subscribe, playbackSettingsStore.get)

  const album = useMemo(() => albums.find((a) => a.id === albumId) || null, [albums, albumId])
  const tracks = useMemo(() => albumId ? getTracksForAlbum(albumId) : [], [albumId, getTracksForAlbum])

  const handleClassicSelect = useCallback(
    (idx: number) => selectTrack({ kind: 'album', albumTracks: tracks }, idx),
    [selectTrack, tracks]
  )

  const handleQueueSingle = useCallback(
    (idx: number) => {
      const track = tracks[idx]
      if (track) addToNowPlaying(track.id)
    },
    [tracks, addToNowPlaying]
  )

  const handleQueueDouble = useCallback(
    (idx: number) => {
      const track = tracks[idx]
      if (track) addToNowPlayingAndPlay(track.id)
    },
    [tracks, addToNowPlayingAndPlay]
  )

  const clickHandler = useClickHandler(handleQueueSingle, handleQueueDouble)

  const handleTrackSelect = clickMode === 'queue-building' ? clickHandler : handleClassicSelect

  const handleShuffleAll = useCallback(() => {
    shuffleAll(tracks)
    onNavigateToNowPlaying()
  }, [shuffleAll, tracks, onNavigateToNowPlaying])

  const handlePlayAlbum = useCallback(() => {
    playAlbum(tracks)
  }, [playAlbum, tracks])

  if (!album) return null

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
          <span>Albums</span>
        </motion.button>
      </div>

      {/* Album header */}
      <div className="flex gap-6 mb-6 shrink-0">
        <div
          className="w-[200px] h-[200px] rounded-xl overflow-hidden shrink-0"
          style={{ background: 'var(--bg-elevated)' }}
        >
          {album.art ? (
            <img src={album.art} alt="" className="w-full h-full object-cover" draggable={false} />
          ) : (
            <div className="w-full h-full flex items-center justify-center" style={{ color: 'var(--text-tertiary)' }}>
              <svg width="64" height="64" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5">
                <circle cx="12" cy="12" r="10" />
                <circle cx="12" cy="12" r="3" />
              </svg>
            </div>
          )}
        </div>
        <div className="flex flex-col justify-end min-w-0">
          <h1 className="font-display text-2xl font-bold truncate" style={{ color: 'var(--text-primary)' }}>
            {album.title}
          </h1>
          <p className="text-base mt-1" style={{ color: 'var(--text-secondary)' }}>
            {album.artist}
          </p>
          <p className="text-sm mt-2" style={{ color: 'var(--text-tertiary)' }}>
            {album.year && <span>{album.year} &middot; </span>}
            {tracks.length} {tracks.length === 1 ? 'track' : 'tracks'}
          </p>
          {tracks.length > 0 && (
            <div className="flex items-center gap-2 mt-3">
              <motion.button
                onClick={handlePlayAlbum}
                className="no-drag flex items-center gap-1.5 px-3 py-1.5 rounded-full text-xs font-medium"
                style={{
                  background: 'var(--accent-dim)',
                  color: 'var(--accent)',
                }}
                whileHover={{ scale: 1.05 }}
                whileTap={{ scale: 0.95 }}
              >
                <PlayIcon size={14} />
                Play
              </motion.button>
              <motion.button
                onClick={handleShuffleAll}
                className="no-drag flex items-center gap-1.5 px-3 py-1.5 rounded-full text-xs font-medium"
                style={{
                  background: 'var(--accent-dim)',
                  color: 'var(--accent)',
                }}
                whileHover={{ scale: 1.05 }}
                whileTap={{ scale: 0.95 }}
              >
                <ShuffleIcon size={14} />
                Shuffle
              </motion.button>
            </div>
          )}
        </div>
      </div>

      {/* Track list */}
      <div className="flex-1 min-h-0 pb-6">
        <TrackList
          tracks={tracks}
          currentTrackId={currentTrack?.id}
          onTrackSelect={handleTrackSelect}
          getAlbumForTrack={getAlbumForTrack}
          isPlaying={isPlaying}
          isTrackFavorited={isTrackFavorited}
          onToggleFavorite={toggleFavorite}
          onAddToPlaylist={addTrackToPlaylist}
          onCreatePlaylist={createPlaylist}
          playlists={playlists}
          isInNowPlaying={clickMode === 'queue-building' ? isInNowPlaying : undefined}
        />
      </div>
    </div>
  )
}
