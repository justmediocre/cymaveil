import { memo } from 'react'
import { formatTime } from '../lib/formatTime'
import { HeartIcon } from './Icons'

export const ROW_HEIGHT = 44

export interface TrackRowProps {
  track: import('../types').Track
  album: import('../types').Album | null
  index: number
  isCurrent: boolean
  isFav: boolean
  isPlaying: boolean
  isQueued?: boolean
  onSelect: (index: number) => void
  onToggleFavorite?: (trackId: string) => void
  onRemoveTrack?: (trackId: string) => void
  onOpenMenu?: (trackId: string, rect: DOMRect) => void
  hasPlaylistMenu: boolean
}

const TrackRow = memo(function TrackRow({
  track,
  album,
  index,
  isCurrent,
  isFav,
  isPlaying,
  isQueued,
  onSelect,
  onToggleFavorite,
  onRemoveTrack,
  onOpenMenu,
  hasPlaylistMenu,
}: TrackRowProps) {
  return (
    <div
      role="button"
      tabIndex={0}
      onClick={() => onSelect(index)}
      onKeyDown={(e: React.KeyboardEvent) => {
        if ((e.key === 'Enter' || e.key === ' ') && e.target === e.currentTarget) {
          e.preventDefault()
          onSelect(index)
        }
      }}
      className="track-row no-drag w-full flex items-center gap-3 px-3 py-2 rounded-lg text-left group cursor-default"
      data-current={isCurrent || undefined}
      style={{
        background: isCurrent ? 'var(--accent-dim)' : 'transparent',
        height: ROW_HEIGHT,
      }}
    >
      {/* Now Playing queue indicator */}
      {isQueued && (
        <span
          className="w-1.5 h-1.5 rounded-full shrink-0"
          style={{ background: 'var(--accent)' }}
        />
      )}

      {/* Track number / playing indicator */}
      <span
        className="font-mono text-xs w-5 text-right shrink-0 tabular-nums"
        style={{ color: isCurrent ? 'var(--accent)' : 'var(--text-tertiary)' }}
      >
        {isCurrent ? (
          <span className="flex items-center justify-end gap-[2px]">
            <span
              className={`inline-block w-[3px] rounded-full${isPlaying ? ' animate-eq-bar-1' : ''}`}
              style={{ background: 'var(--accent)', height: isPlaying ? undefined : 6 }}
            />
            <span
              className={`inline-block w-[3px] rounded-full${isPlaying ? ' animate-eq-bar-2' : ''}`}
              style={{ background: 'var(--accent)', height: isPlaying ? undefined : 6 }}
            />
            <span
              className={`inline-block w-[3px] rounded-full${isPlaying ? ' animate-eq-bar-3' : ''}`}
              style={{ background: 'var(--accent)', height: isPlaying ? undefined : 6 }}
            />
          </span>
        ) : (
          track.trackNum
        )}
      </span>

      {/* Mini album art */}
      <div
        className="w-8 h-8 rounded shrink-0 overflow-hidden"
        style={{ background: 'var(--bg-elevated)' }}
      >
        {album && (
          <img
            src={album.art ?? undefined}
            alt=""
            className="w-full h-full object-cover"
            draggable={false}
          />
        )}
      </div>

      {/* Track info */}
      <div className="flex-1 min-w-0">
        <p
          className="text-sm font-medium truncate"
          style={{ color: 'var(--text-primary)' }}
        >
          {track.title}
        </p>
        <p className="text-xs truncate" style={{ color: 'var(--text-tertiary)' }}>
          {track.artist || album?.artist}
        </p>
      </div>

      {/* Action buttons — visible on hover */}
      <div className="flex items-center gap-1 shrink-0">
        {/* Heart / favorite toggle */}
        {onToggleFavorite && (
          <button
            type="button"
            onClick={(e: React.MouseEvent) => { e.stopPropagation(); onToggleFavorite(track.id) }}
            className={`track-action-btn flex items-center justify-center w-6 h-6 rounded-full transition-opacity cursor-pointer ${isFav ? 'opacity-100' : 'opacity-0 group-hover:opacity-100 focus-visible:opacity-100'}`}
            style={{ color: isFav ? 'var(--accent)' : 'var(--text-tertiary)' }}
            aria-label="Toggle favorite"
          >
            <HeartIcon size={13} filled={isFav} />
          </button>
        )}

        {/* Add to playlist — trigger button only, menu is hoisted */}
        {hasPlaylistMenu && onOpenMenu && (
          <button
            type="button"
            onClick={(e: React.MouseEvent) => {
              e.stopPropagation()
              const rect = (e.currentTarget as HTMLElement).getBoundingClientRect()
              onOpenMenu(track.id, rect)
            }}
            className="track-action-btn flex items-center justify-center w-6 h-6 rounded-full opacity-0 group-hover:opacity-100 focus-visible:opacity-100 transition-opacity cursor-pointer"
            style={{ color: 'var(--text-tertiary)' }}
            aria-label="Add to playlist"
          >
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round">
              <line x1="12" y1="5" x2="12" y2="19" />
              <line x1="5" y1="12" x2="19" y2="12" />
            </svg>
          </button>
        )}

        {/* Remove from playlist */}
        {onRemoveTrack && (
          <button
            type="button"
            onClick={(e: React.MouseEvent) => { e.stopPropagation(); onRemoveTrack(track.id) }}
            className="track-action-btn flex items-center justify-center w-6 h-6 rounded-full opacity-0 group-hover:opacity-100 focus-visible:opacity-100 transition-opacity cursor-pointer"
            style={{ color: 'var(--text-tertiary)' }}
            title="Remove from playlist"
            aria-label="Remove from playlist"
          >
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round">
              <line x1="18" y1="6" x2="6" y2="18" />
              <line x1="6" y1="6" x2="18" y2="18" />
            </svg>
          </button>
        )}
      </div>

      {/* Duration */}
      <span
        className="font-mono text-[11px] tabular-nums shrink-0"
        style={{ color: 'var(--text-tertiary)' }}
      >
        {formatTime(track.duration)}
      </span>
    </div>
  )
})

export default TrackRow
