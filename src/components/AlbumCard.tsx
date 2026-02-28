import { motion } from 'motion/react'
import type { Album } from '../types'

interface AlbumCardProps {
  album: Album
  onClick: (albumId: string) => void
}

export default function AlbumCard({ album, onClick }: AlbumCardProps) {
  return (
    <motion.button
      onClick={() => onClick(album.id)}
      className="text-left w-full group"
      whileHover={{ scale: 1.03 }}
      whileTap={{ scale: 0.97 }}
    >
      <div
        className="aspect-square rounded-xl overflow-hidden mb-2"
        style={{ background: 'var(--bg-elevated)' }}
      >
        {album.art ? (
          <img src={album.art} alt="" className="w-full h-full object-cover" draggable={false} />
        ) : (
          <div className="w-full h-full flex items-center justify-center" style={{ color: 'var(--text-tertiary)' }}>
            <svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5">
              <circle cx="12" cy="12" r="10" />
              <circle cx="12" cy="12" r="3" />
            </svg>
          </div>
        )}
      </div>
      <p className="text-sm font-medium truncate" style={{ color: 'var(--text-primary)' }}>
        {album.title}
      </p>
      <p className="text-xs truncate" style={{ color: 'var(--text-tertiary)' }}>
        {album.artist}
      </p>
    </motion.button>
  )
}
