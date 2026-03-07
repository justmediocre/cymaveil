import { useState, useRef, useEffect, useLayoutEffect, useCallback } from 'react'
import { motion, AnimatePresence } from 'motion/react'
import NewPlaylistInput from './NewPlaylistInput'
import type { Playlist } from '../types'

interface AddToPlaylistMenuProps {
  trackId: string
  playlists: Playlist[]
  onAddToPlaylist: (playlistId: string, trackId: string) => void
  onCreatePlaylist: (name: string) => Playlist | null
  // Portal mode: positioned absolutely via anchorRect, with onClose callback
  anchorRect?: DOMRect
  onClose?: () => void
  // Inline mode (legacy): renders trigger button + relative menu
  alwaysVisible?: boolean
}

export default function AddToPlaylistMenu({
  trackId,
  playlists,
  onAddToPlaylist,
  onCreatePlaylist,
  anchorRect,
  onClose,
  alwaysVisible = false,
}: AddToPlaylistMenuProps) {
  const isPortal = !!(anchorRect && onClose)
  const [isOpen, setIsOpen] = useState<boolean>(isPortal)
  const [showNewInput, setShowNewInput] = useState<boolean>(false)
  const menuRef = useRef<HTMLDivElement | null>(null)

  const closeMenu = useCallback(() => {
    setIsOpen(false)
    setShowNewInput(false)
    onClose?.()
  }, [onClose])

  // Close on outside click
  useEffect(() => {
    if (!isOpen && !isPortal) return
    const handleClick = (e: MouseEvent) => {
      if (menuRef.current && !menuRef.current.contains(e.target as Node)) {
        closeMenu()
      }
    }
    document.addEventListener('mousedown', handleClick)
    return () => document.removeEventListener('mousedown', handleClick)
  }, [isOpen, isPortal, closeMenu])

  // Close on scroll (portal mode)
  useEffect(() => {
    if (!isPortal) return
    const handleScroll = () => closeMenu()
    window.addEventListener('scroll', handleScroll, { capture: true, passive: true })
    return () => window.removeEventListener('scroll', handleScroll, { capture: true } as EventListenerOptions)
  }, [isPortal, closeMenu])

  const handleCreate = (name: string) => {
    const playlist = onCreatePlaylist(name)
    if (playlist) {
      onAddToPlaylist(playlist.id, trackId)
    }
    closeMenu()
  }

  // Filter out favorites — favorites uses the heart icon
  const userPlaylists = playlists.filter((p) => p.id !== 'favorites')

  // Viewport-clamped position for portal mode
  const [portalPos, setPortalPos] = useState<React.CSSProperties | null>(null)

  useLayoutEffect(() => {
    if (!isPortal || !anchorRect || !menuRef.current) return
    const { width: mw, height: mh } = menuRef.current.getBoundingClientRect()
    const pad = 8

    // Default: above button, right-aligned to button right edge
    let top = anchorRect.top - 4 - mh
    let left = anchorRect.right - mw

    // Flip below if overflowing top
    if (top < pad) top = anchorRect.bottom + 4

    // Clamp to viewport
    top = Math.max(pad, Math.min(top, window.innerHeight - mh - pad))
    left = Math.max(pad, Math.min(left, window.innerWidth - mw - pad))

    setPortalPos({ position: 'fixed', zIndex: 9999, top, left })
  }, [isPortal, anchorRect])

  const menuStyle: React.CSSProperties = isPortal && anchorRect
    ? (portalPos ?? {
        position: 'fixed',
        zIndex: 9999,
        top: anchorRect.top - 4,
        left: anchorRect.right,
        transform: 'translate(-100%, -100%)',
      })
    : {}

  const menuContent = (
    <motion.div
      ref={menuRef}
      className={isPortal ? '' : 'absolute right-0 bottom-full mb-1 z-50'}
      style={{
        ...menuStyle,
        background: 'var(--glass-bg-surface)',
        border: '1px solid var(--border-subtle)',
        minWidth: 180,
        borderRadius: 12,
        overflow: 'hidden',
        boxShadow: '0 8px 32px rgba(0,0,0,0.3)',
      }}
      initial={{ opacity: 0, y: 4, scale: 0.95 }}
      animate={{ opacity: 1, y: 0, scale: 1 }}
      exit={{ opacity: 0, y: 4, scale: 0.95 }}
      transition={{ duration: 0.15 }}
      onClick={(e: React.MouseEvent) => e.stopPropagation()}
    >
      <div className="py-1">
        <p
          className="px-3 py-1.5 text-[10px] uppercase tracking-wider font-medium"
          style={{ color: 'var(--text-tertiary)' }}
        >
          Add to playlist
        </p>

        {userPlaylists.map((playlist) => {
          const isInPlaylist = playlist.trackIds.includes(trackId)
          return (
            <button
              key={playlist.id}
              onClick={() => {
                onAddToPlaylist(playlist.id, trackId)
                closeMenu()
              }}
              className="w-full text-left px-3 py-2 text-xs flex items-center gap-2 transition-colors hover:[background:var(--bg-hover)]"
              style={{ color: 'var(--text-primary)' }}
            >
              {isInPlaylist && (
                <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                  <polyline points="20 6 9 17 4 12" />
                </svg>
              )}
              <span className={isInPlaylist ? '' : 'pl-[20px]'}>{playlist.name}</span>
            </button>
          )
        })}

        {/* Divider */}
        <div className="mx-3 my-1" style={{ height: 1, background: 'var(--border-subtle)' }} />

        {showNewInput ? (
          <NewPlaylistInput
            compact
            onSubmit={handleCreate}
            onCancel={closeMenu}
          />
        ) : (
          <button
            onClick={() => setShowNewInput(true)}
            className="w-full text-left px-3 py-2 text-xs transition-colors hover:[background:var(--bg-hover)]"
            style={{ color: 'var(--text-secondary)' }}
          >
            + New Playlist
          </button>
        )}
      </div>
    </motion.div>
  )

  // Portal mode — menu is always open, rendered directly
  if (isPortal) {
    return menuContent
  }

  // Inline mode (legacy — used by NowPlaying etc.)
  return (
    <div className="relative" ref={menuRef}>
      <motion.button
        onClick={(e: React.MouseEvent) => { e.stopPropagation(); setIsOpen(!isOpen) }}
        className={`flex items-center justify-center w-6 h-6 rounded-full transition-opacity ${alwaysVisible ? 'opacity-100' : 'opacity-0 group-hover:opacity-100'}`}
        style={{ color: 'var(--text-tertiary)' }}
        aria-label="Add to playlist"
        whileHover={{ color: 'var(--text-primary)', scale: 1.1 }}
        whileTap={{ scale: 0.9 }}
      >
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round">
          <line x1="12" y1="5" x2="12" y2="19" />
          <line x1="5" y1="12" x2="19" y2="12" />
        </svg>
      </motion.button>

      <AnimatePresence>
        {isOpen && menuContent}
      </AnimatePresence>
    </div>
  )
}
