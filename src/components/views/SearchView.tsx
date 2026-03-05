import { useState, useTransition, useMemo, useRef, useEffect, useCallback } from 'react'
import { useLibraryCtx } from '../../contexts/library/LibraryContext'
import { usePlaylistCtx } from '../../contexts/playlist/PlaylistContext'
import { usePlayback } from '../../contexts/playback/PlaybackContext'
import { SearchIcon } from '../Icons'
import AlbumCard from '../AlbumCard'
import TrackList from '../TrackList'
import { useTrackClickHandler } from '../../hooks/useClickMode'

interface SearchViewProps {
  onAlbumSelect: (albumId: string) => void
}

export default function SearchView({ onAlbumSelect }: SearchViewProps) {
  const { albums, tracks, getAlbumForTrack, getTracksForAlbum } = useLibraryCtx()
  const { isTrackFavorited, toggleFavorite, addTrackToPlaylist, createPlaylist, playlists, isInNowPlaying } = usePlaylistCtx()
  const { currentTrack, isPlaying, selectTrack, playAlbum } = usePlayback()

  const [query, setQuery] = useState('')
  const [deferredQuery, setDeferredQuery] = useState('')
  const [isPending, startTransition] = useTransition()
  const inputRef = useRef<HTMLInputElement | null>(null)

  useEffect(() => {
    inputRef.current?.focus()
  }, [])

  const lowerQuery = deferredQuery.toLowerCase().trim()

  const filteredAlbums = useMemo(() => {
    if (!lowerQuery) return []
    return albums.filter(
      (a) => a.title.toLowerCase().includes(lowerQuery) || a.artist.toLowerCase().includes(lowerQuery)
    )
  }, [albums, lowerQuery])

  const filteredTracks = useMemo(() => {
    if (!lowerQuery) return []
    return tracks.filter((t) => {
      const album = getAlbumForTrack(t)
      return (
        t.title.toLowerCase().includes(lowerQuery) ||
        (album?.artist || '').toLowerCase().includes(lowerQuery)
      )
    })
  }, [tracks, lowerQuery, getAlbumForTrack])

  const handleClassicSelect = useCallback(
    (idx: number) => selectTrack({ kind: 'global', trackList: filteredTracks }, idx),
    [selectTrack, filteredTracks]
  )

  const { handleTrackSelect, isQueueBuilding } = useTrackClickHandler(filteredTracks, handleClassicSelect)

  const handlePlayAlbum = useCallback(
    (albumId: string) => {
      const albumTracks = getTracksForAlbum(albumId)
      if (albumTracks.length > 0) playAlbum(albumTracks)
    },
    [getTracksForAlbum, playAlbum]
  )

  const hasAlbums = filteredAlbums.length > 0
  const hasTracks = filteredTracks.length > 0

  return (
    <div className="h-full flex flex-col overflow-hidden px-10">
      {/* Search input */}
      <div className="pt-6 pb-5 shrink-0">
        <div
          className="flex items-center gap-3 px-4 py-3 rounded-xl"
          style={{ background: 'var(--bg-elevated)' }}
        >
          <SearchIcon size={18} style={{ color: 'var(--text-tertiary)', flexShrink: 0 }} />
          <input
            ref={inputRef}
            type="text"
            value={query}
            onChange={(e: React.ChangeEvent<HTMLInputElement>) => {
              setQuery(e.target.value)
              startTransition(() => setDeferredQuery(e.target.value))
            }}
            placeholder="Search tracks and albums..."
            className="bg-transparent border-none w-full text-sm"
            style={{ color: 'var(--text-primary)' }}
          />
        </div>
      </div>

      {!query.trim() ? (
        <div className="flex items-center justify-center h-48">
          <p className="text-sm" style={{ color: 'var(--text-tertiary)' }}>
            Start typing to search
          </p>
        </div>
      ) : (
        <div className="flex-1 min-h-0 flex flex-col overflow-hidden pb-6" style={{ opacity: isPending ? 0.7 : 1, transition: 'opacity 100ms' }}>
          {/* Albums section */}
          {hasAlbums && (
            <div className="mb-6 shrink-0 overflow-y-auto" style={{ maxHeight: '40%' }}>
              <h2
                className="font-display text-xs font-bold tracking-wider uppercase mb-3"
                style={{ color: 'var(--text-tertiary)' }}
              >
                Albums
              </h2>
              <div
                className="grid gap-4"
                style={{ gridTemplateColumns: 'repeat(auto-fill, minmax(160px, 1fr))' }}
              >
                {filteredAlbums.map((album) => (
                  <AlbumCard key={album.id} album={album} onClick={onAlbumSelect} onPlayAlbum={handlePlayAlbum} />
                ))}
              </div>
            </div>
          )}

          {/* Tracks section */}
          {hasTracks && (
            <div className="flex-1 min-h-0 flex flex-col">
              <h2
                className="font-display text-xs font-bold tracking-wider uppercase mb-3 shrink-0"
                style={{ color: 'var(--text-tertiary)' }}
              >
                Tracks
              </h2>
              <div className="flex-1 min-h-0">
                <TrackList
                  tracks={filteredTracks}
                  currentTrackId={currentTrack?.id}
                  onTrackSelect={handleTrackSelect}
                  getAlbumForTrack={getAlbumForTrack}
                  isPlaying={isPlaying}
                  isTrackFavorited={isTrackFavorited}
                  onToggleFavorite={toggleFavorite}
                  onAddToPlaylist={addTrackToPlaylist}
                  onCreatePlaylist={createPlaylist}
                  playlists={playlists}
                  isInNowPlaying={isQueueBuilding ? isInNowPlaying : undefined}
                />
              </div>
            </div>
          )}

          {/* No results */}
          {!hasAlbums && !hasTracks && (
            <div className="flex items-center justify-center h-48">
              <p className="text-sm" style={{ color: 'var(--text-tertiary)' }}>
                No results for "{query}"
              </p>
            </div>
          )}
        </div>
      )}
    </div>
  )
}
