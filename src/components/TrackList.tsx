import { useState, useRef, useEffect, useCallback, useMemo } from 'react'
import { useVirtualizer } from '@tanstack/react-virtual'
import { createPortal } from 'react-dom'
import TrackRow, { ROW_HEIGHT } from './TrackRow'
import AddToPlaylistMenu from './AddToPlaylistMenu'
import type { Track, Album, Playlist } from '../types'

interface TrackListProps {
  tracks: Track[]
  currentTrackId?: string | null
  onTrackSelect: (index: number) => void
  getAlbumForTrack: (track: Track) => Album | null
  autoScrollToCurrent?: boolean
  isPlaying?: boolean
  isTrackFavorited?: (trackId: string) => boolean
  onToggleFavorite?: (trackId: string) => void
  onRemoveTrack?: (trackId: string) => void
  onAddToPlaylist?: (playlistId: string, trackId: string) => void
  onCreatePlaylist?: (name: string) => Playlist | null
  playlists?: Playlist[]
}

// ── TrackList ─────────────────────────────────────────────────────────────────

export default function TrackList({
  tracks,
  currentTrackId,
  onTrackSelect,
  getAlbumForTrack,
  autoScrollToCurrent = false,
  isPlaying = false,
  isTrackFavorited,
  onToggleFavorite,
  onRemoveTrack,
  onAddToPlaylist,
  onCreatePlaylist,
  playlists,
}: TrackListProps) {
  const scrollContainerRef = useRef<HTMLDivElement | null>(null)
  const needsScrollRef = useRef(false)
  const hasScrolledOnceRef = useRef(false)

  // Shared playlist menu state (hoisted from individual rows)
  const [menuTrackId, setMenuTrackId] = useState<string | null>(null)
  const [menuRect, setMenuRect] = useState<DOMRect | null>(null)

  const hasPlaylistMenu = !!(onAddToPlaylist && playlists)

  const virtualizer = useVirtualizer({
    count: tracks.length,
    getScrollElement: () => scrollContainerRef.current,
    estimateSize: () => ROW_HEIGHT,
    overscan: 10,
  })

  // Pre-compute album map for O(1) lookups within this render
  const albumsByTrack = useMemo(() => {
    const map = new Map<string, Album | null>()
    for (const track of tracks) {
      if (!map.has(track.id)) {
        map.set(track.id, getAlbumForTrack(track))
      }
    }
    return map
  }, [tracks, getAlbumForTrack])

  // Pre-compute favorites set for O(1) per-row
  const favSet = useMemo(() => {
    if (!isTrackFavorited) return new Set<string>()
    const set = new Set<string>()
    for (const track of tracks) {
      if (isTrackFavorited(track.id)) set.add(track.id)
    }
    return set
  }, [tracks, isTrackFavorited])

  // Flag that we need to scroll when the current track changes
  useEffect(() => {
    if (autoScrollToCurrent && currentTrackId) {
      needsScrollRef.current = true
    }
  }, [currentTrackId, autoScrollToCurrent])

  // Auto-scroll to current track using virtualizer
  // Use instant scroll on initial mount so the list opens already positioned,
  // then smooth scroll for subsequent track changes.
  useEffect(() => {
    if (!needsScrollRef.current || !currentTrackId) return
    const idx = tracks.findIndex((t) => t.id === currentTrackId)
    if (idx >= 0) {
      const behavior = hasScrolledOnceRef.current ? 'smooth' : 'auto'
      virtualizer.scrollToIndex(idx, { align: 'center', behavior })
      needsScrollRef.current = false
      hasScrolledOnceRef.current = true
    }
  }, [currentTrackId, tracks, virtualizer])

  // Stable callback for row selection
  const handleSelect = useCallback(
    (index: number) => onTrackSelect(index),
    [onTrackSelect]
  )

  // Stable callback for opening the playlist menu
  const handleOpenMenu = useCallback((trackId: string, rect: DOMRect) => {
    setMenuTrackId(trackId)
    setMenuRect(rect)
  }, [])

  // Close the shared menu
  const handleCloseMenu = useCallback(() => {
    setMenuTrackId(null)
    setMenuRect(null)
  }, [])

  const virtualItems = virtualizer.getVirtualItems()
  const totalSize = virtualizer.getTotalSize()

  return (
    <div
      ref={scrollContainerRef}
      className="overflow-y-auto overflow-x-hidden"
      style={{ height: '100%' }}
    >
      <div
        style={{
          height: totalSize,
          width: '100%',
          position: 'relative',
        }}
      >
        {virtualItems.map((virtualRow) => {
          const track = tracks[virtualRow.index]!
          const isCurrent = track.id === currentTrackId
          return (
            <div
              key={track.id}
              style={{
                position: 'absolute',
                top: 0,
                left: 0,
                width: '100%',
                height: virtualRow.size,
                transform: `translateY(${virtualRow.start}px)`,
              }}
            >
              <TrackRow
                track={track}
                album={albumsByTrack.get(track.id) ?? null}
                index={virtualRow.index}
                isCurrent={isCurrent}
                isFav={favSet.has(track.id)}
                isPlaying={isCurrent && isPlaying}
                onSelect={handleSelect}
                onToggleFavorite={onToggleFavorite}
                onRemoveTrack={onRemoveTrack}
                onOpenMenu={hasPlaylistMenu ? handleOpenMenu : undefined}
                hasPlaylistMenu={hasPlaylistMenu}
              />
            </div>
          )
        })}
      </div>

      {/* Single shared AddToPlaylistMenu — rendered as portal */}
      {menuTrackId && menuRect && hasPlaylistMenu && createPortal(
        <AddToPlaylistMenu
          trackId={menuTrackId}
          playlists={playlists!}
          onAddToPlaylist={onAddToPlaylist!}
          onCreatePlaylist={onCreatePlaylist!}
          anchorRect={menuRect}
          onClose={handleCloseMenu}
        />,
        document.body,
      )}
    </div>
  )
}
