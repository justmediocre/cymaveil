import { useState, useRef, useCallback, useMemo } from 'react'
import { useVirtualizer } from '@tanstack/react-virtual'
import { createPortal } from 'react-dom'
import { motion } from 'motion/react'
import { useLibraryCtx } from '../../contexts/library/LibraryContext'
import { usePlaylistCtx } from '../../contexts/playlist/PlaylistContext'
import { usePlayback } from '../../contexts/playback/PlaybackContext'
import TrackRow, { ROW_HEIGHT } from '../TrackRow'
import AddToPlaylistMenu from '../AddToPlaylistMenu'
import { ShuffleIcon } from '../Icons'
import EmptyState from '../EmptyState'
import { useTrackClickHandler } from '../../hooks/useClickMode'
import type { Track, Album } from '../../types'

const HEADER_HEIGHT = 36

function getLetterForTitle(title: string): string {
  const first = title.trimStart().charAt(0).toUpperCase()
  if (first >= 'A' && first <= 'Z') return first
  return '#'
}

type VirtualItem =
  | { type: 'header'; letter: string }
  | { type: 'track'; track: Track; sortedIndex: number }

interface LibraryViewProps {
  onNavigateToNowPlaying: () => void
}

export default function LibraryView({ onNavigateToNowPlaying }: LibraryViewProps) {
  const { tracks, getAlbumForTrack } = useLibraryCtx()
  const { isTrackFavorited, toggleFavorite, addTrackToPlaylist, createPlaylist, playlists, isInNowPlaying } = usePlaylistCtx()
  const { currentTrack, isPlaying, selectTrack, shuffleAll } = usePlayback()

  const scrollContainerRef = useRef<HTMLDivElement | null>(null)

  // Playlist menu state
  const [menuTrackId, setMenuTrackId] = useState<string | null>(null)
  const [menuRect, setMenuRect] = useState<DOMRect | null>(null)
  const hasPlaylistMenu = playlists.length > 0

  // Sort tracks alphabetically by title
  const sortedTracks = useMemo(
    () => [...tracks].sort((a, b) => a.title.localeCompare(b.title, undefined, { sensitivity: 'base' })),
    [tracks]
  )

  // Build flat items array with section headers interleaved
  const { items, letterIndices, availableLetters } = useMemo(() => {
    const items: VirtualItem[] = []
    const letterIndices = new Map<string, number>() // letter -> index into items
    const availableLetters: string[] = []
    let currentLetter = ''

    for (let i = 0; i < sortedTracks.length; i++) {
      const track = sortedTracks[i]!
      const letter = getLetterForTitle(track.title)
      if (letter !== currentLetter) {
        currentLetter = letter
        letterIndices.set(letter, items.length)
        availableLetters.push(letter)
        items.push({ type: 'header', letter })
      }
      items.push({ type: 'track', track, sortedIndex: i })
    }

    return { items, letterIndices, availableLetters }
  }, [sortedTracks])

  // Virtualizer with variable row heights
  const virtualizer = useVirtualizer({
    count: items.length,
    getScrollElement: () => scrollContainerRef.current,
    estimateSize: (index) => items[index]?.type === 'header' ? HEADER_HEIGHT : ROW_HEIGHT,
    overscan: 10,
  })

  // Track selection
  const handleClassicSelect = useCallback(
    (index: number) => selectTrack({ kind: 'global', trackList: sortedTracks }, index),
    [selectTrack, sortedTracks]
  )

  const { handleTrackSelect: handleSelect, isQueueBuilding } = useTrackClickHandler(sortedTracks, handleClassicSelect)

  // Pre-compute album map + favorites set + now-playing set in a single pass
  const { albumsByTrack, favSet, queuedSet } = useMemo(() => {
    const albumsByTrack = new Map<string, Album | null>()
    const favSet = new Set<string>()
    const queuedSet = new Set<string>()
    for (const track of sortedTracks) {
      if (!albumsByTrack.has(track.id)) {
        albumsByTrack.set(track.id, getAlbumForTrack(track))
      }
      if (isTrackFavorited(track.id)) favSet.add(track.id)
      if (isQueueBuilding && isInNowPlaying(track.id)) queuedSet.add(track.id)
    }
    return { albumsByTrack, favSet, queuedSet }
  }, [sortedTracks, getAlbumForTrack, isTrackFavorited, isInNowPlaying, isQueueBuilding])

  // Shuffle all
  const handleShuffleAll = useCallback(() => {
    shuffleAll(tracks)
    onNavigateToNowPlaying()
  }, [shuffleAll, tracks, onNavigateToNowPlaying])

  // Playlist menu callbacks
  const handleOpenMenu = useCallback((trackId: string, rect: DOMRect) => {
    setMenuTrackId(trackId)
    setMenuRect(rect)
  }, [])

  const handleCloseMenu = useCallback(() => {
    setMenuTrackId(null)
    setMenuRect(null)
  }, [])

  // Jump to letter
  const scrollToLetter = useCallback(
    (letter: string) => {
      const idx = letterIndices.get(letter)
      if (idx !== undefined) {
        virtualizer.scrollToIndex(idx, { align: 'start' })
      }
    },
    [letterIndices, virtualizer]
  )

  // Determine currently visible letter from scroll position
  const virtualItems = virtualizer.getVirtualItems()
  const scrollOffset = virtualizer.scrollOffset ?? 0
  let visibleLetter = availableLetters[0] ?? ''
  for (const vi of virtualItems) {
    if (vi.start + vi.size <= scrollOffset) continue
    const item = items[vi.index]
    if (item?.type === 'header') {
      visibleLetter = item.letter
      break
    }
    if (item?.type === 'track') {
      visibleLetter = getLetterForTitle(item.track.title)
      break
    }
  }

  if (tracks.length === 0) {
    return (
      <EmptyState
        icon="music"
        title="No tracks yet"
        subtitle="Add a music folder to get started"
        showImport
      />
    )
  }

  const totalSize = virtualizer.getTotalSize()
  const currentTrackId = currentTrack?.id

  return (
    <div className="h-full flex flex-col overflow-hidden px-10">
      <div className="flex items-center gap-3 pt-6 pb-5 shrink-0">
        <h1 className="font-display text-2xl font-bold" style={{ color: 'var(--text-primary)' }}>
          Library
        </h1>
        <span className="text-sm" style={{ color: 'var(--text-tertiary)' }}>
          {tracks.length} {tracks.length === 1 ? 'track' : 'tracks'}
        </span>
        {tracks.length > 0 && (
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

      <div className="flex-1 min-h-0 pb-6 flex">
        {/* Scrollable track list */}
        <div
          ref={scrollContainerRef}
          className="flex-1 overflow-y-auto overflow-x-hidden"
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
              const item = items[virtualRow.index]!
              if (item.type === 'header') {
                return (
                  <div
                    key={`header-${item.letter}`}
                    style={{
                      position: 'absolute',
                      top: 0,
                      left: 0,
                      width: '100%',
                      height: virtualRow.size,
                      transform: `translateY(${virtualRow.start}px)`,
                    }}
                  >
                    <div
                      className="flex items-end px-3 font-display text-sm font-bold"
                      style={{
                        height: HEADER_HEIGHT,
                        color: 'var(--text-secondary)',
                        borderBottom: '1px solid var(--border-subtle)',
                        paddingBottom: 4,
                      }}
                    >
                      {item.letter}
                    </div>
                  </div>
                )
              }

              const { track, sortedIndex } = item
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
                    index={sortedIndex}
                    isCurrent={isCurrent}
                    isFav={favSet.has(track.id)}
                    isPlaying={isCurrent && isPlaying}
                    isQueued={queuedSet.has(track.id)}
                    onSelect={handleSelect}
                    onToggleFavorite={toggleFavorite}
                    onOpenMenu={hasPlaylistMenu ? handleOpenMenu : undefined}
                    hasPlaylistMenu={hasPlaylistMenu}
                  />
                </div>
              )
            })}
          </div>
        </div>

        {/* Alphabet sidebar */}
        {availableLetters.length > 1 && (
          <div
            className="shrink-0 flex flex-col items-center overflow-hidden ml-1"
            style={{ width: 20 }}
          >
            {availableLetters.map((letter) => (
              <button
                key={letter}
                onClick={() => scrollToLetter(letter)}
                className="flex-1 min-h-0 flex items-center justify-center text-[10px] font-bold leading-none transition-colors"
                style={{
                  width: 18,
                  maxHeight: 22,
                  color: letter === visibleLetter ? 'var(--accent)' : 'var(--text-tertiary)',
                  borderRadius: 4,
                  background: letter === visibleLetter ? 'var(--accent-dim)' : 'transparent',
                }}
              >
                {letter}
              </button>
            ))}
          </div>
        )}
      </div>

      {/* Single shared AddToPlaylistMenu — rendered as portal */}
      {menuTrackId && menuRect && hasPlaylistMenu && createPortal(
        <AddToPlaylistMenu
          trackId={menuTrackId}
          playlists={playlists}
          onAddToPlaylist={addTrackToPlaylist}
          onCreatePlaylist={createPlaylist}
          anchorRect={menuRect}
          onClose={handleCloseMenu}
        />,
        document.body,
      )}
    </div>
  )
}
