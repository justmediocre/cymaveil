import { useState } from 'react'
import { motion } from 'motion/react'

interface NewPlaylistInputProps {
  onSubmit: (name: string) => void
  onCancel: () => void
  compact?: boolean
}

export default function NewPlaylistInput({ onSubmit, onCancel, compact = false }: NewPlaylistInputProps) {
  const [name, setName] = useState('')

  const handleSubmit = () => {
    const trimmed = name.trim()
    if (!trimmed) return
    onSubmit(trimmed)
  }

  const handleKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter') handleSubmit()
    if (e.key === 'Escape') onCancel()
  }

  if (compact) {
    return (
      <div className="px-3 py-2 flex items-center gap-1">
        <input
          autoFocus
          type="text"
          value={name}
          onChange={(e: React.ChangeEvent<HTMLInputElement>) => setName(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder="Name..."
          className="bg-transparent border-none text-xs flex-1 min-w-0"
          style={{ color: 'var(--text-primary)' }}
        />
        <button
          onClick={handleSubmit}
          className="text-xs font-medium px-2 py-0.5 rounded"
          style={{ color: 'var(--accent)' }}
        >
          Add
        </button>
      </div>
    )
  }

  return (
    <div className="flex items-center gap-2 mb-4">
      <input
        autoFocus
        type="text"
        value={name}
        onChange={(e: React.ChangeEvent<HTMLInputElement>) => setName(e.target.value)}
        onKeyDown={handleKeyDown}
        placeholder="Playlist name..."
        className="bg-transparent border-none text-sm flex-1 px-3 py-2 rounded-lg"
        style={{ color: 'var(--text-primary)', background: 'var(--bg-elevated)' }}
      />
      <motion.button
        onClick={handleSubmit}
        className="px-3 py-2 rounded-lg text-xs font-medium"
        style={{ background: 'var(--accent-dim)', color: 'var(--accent)' }}
        whileHover={{ scale: 1.05 }}
        whileTap={{ scale: 0.95 }}
      >
        Create
      </motion.button>
      <motion.button
        onClick={onCancel}
        className="px-3 py-2 rounded-lg text-xs"
        style={{ color: 'var(--text-tertiary)' }}
        whileTap={{ scale: 0.95 }}
      >
        Cancel
      </motion.button>
    </div>
  )
}
