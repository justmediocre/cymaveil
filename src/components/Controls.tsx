import { memo } from 'react'
import { motion } from 'motion/react'
import { PlayIcon, PauseIcon, SkipForwardIcon, SkipBackIcon, ShuffleIcon, RepeatIcon, RepeatOneIcon } from './Icons'

interface ControlsProps {
  isPlaying: boolean
  onPlayPause: () => void
  onNext: () => void
  onPrev: () => void
  shuffle: boolean
  repeat: 'off' | 'all' | 'one'
  onShuffleToggle: () => void
  onRepeatToggle: () => void
}

export default memo(function Controls({ isPlaying, onPlayPause, onNext, onPrev, shuffle, repeat, onShuffleToggle, onRepeatToggle }: ControlsProps) {
  return (
    <div className="flex items-center justify-center gap-2">
      {/* Shuffle */}
      <motion.button
        onClick={(e: React.MouseEvent) => { (e.currentTarget as HTMLButtonElement).blur(); onShuffleToggle() }}
        className="no-drag relative flex items-center justify-center w-10 h-10 rounded-full"
        aria-label={shuffle ? 'Disable shuffle' : 'Enable shuffle'}
        style={{ color: shuffle ? 'var(--accent)' : 'var(--text-tertiary)' }}
        whileHover={{ scale: 1.1, color: 'var(--text-primary)' }}
        whileTap={{ scale: 0.9 }}
      >
        <ShuffleIcon size={16} />
        {shuffle && (
          <div
            className="absolute -bottom-0.5 left-1/2 -translate-x-1/2 w-1 h-1 rounded-full"
            style={{ background: 'var(--accent)' }}
          />
        )}
      </motion.button>

      {/* Previous */}
      <motion.button
        onClick={(e: React.MouseEvent) => { (e.currentTarget as HTMLButtonElement).blur(); onPrev() }}
        className="no-drag flex items-center justify-center w-10 h-10 rounded-full"
        aria-label="Previous track"
        style={{ color: 'var(--text-secondary)' }}
        whileHover={{ scale: 1.1, color: 'var(--text-primary)' }}
        whileTap={{ scale: 0.85 }}
      >
        <SkipBackIcon size={20} />
      </motion.button>

      {/* Play / Pause — the hero button */}
      <motion.button
        onClick={(e: React.MouseEvent) => { (e.currentTarget as HTMLButtonElement).blur(); onPlayPause() }}
        className="no-drag flex items-center justify-center w-12 h-12 rounded-full"
        aria-label={isPlaying ? 'Pause' : 'Play'}
        style={{
          background: 'var(--text-primary)',
          color: 'var(--bg-primary)',
        }}
        whileHover={{ scale: 1.08 }}
        whileTap={{ scale: 0.92 }}
      >
        <motion.div
          key={isPlaying ? 'pause' : 'play'}
          initial={{ scale: 0.5, opacity: 0 }}
          animate={{ scale: 1, opacity: 1 }}
          transition={{ duration: 0.2, ease: 'easeOut' }}
        >
          {isPlaying ? <PauseIcon size={22} /> : <PlayIcon size={22} />}
        </motion.div>
      </motion.button>

      {/* Next */}
      <motion.button
        onClick={(e: React.MouseEvent) => { (e.currentTarget as HTMLButtonElement).blur(); onNext() }}
        className="no-drag flex items-center justify-center w-10 h-10 rounded-full"
        aria-label="Next track"
        style={{ color: 'var(--text-secondary)' }}
        whileHover={{ scale: 1.1, color: 'var(--text-primary)' }}
        whileTap={{ scale: 0.85 }}
      >
        <SkipForwardIcon size={20} />
      </motion.button>

      {/* Repeat */}
      <motion.button
        onClick={(e: React.MouseEvent) => { (e.currentTarget as HTMLButtonElement).blur(); onRepeatToggle() }}
        className="no-drag relative flex items-center justify-center w-10 h-10 rounded-full"
        aria-label={repeat === 'off' ? 'Enable repeat' : repeat === 'all' ? 'Repeat one' : 'Disable repeat'}
        style={{ color: repeat !== 'off' ? 'var(--accent)' : 'var(--text-tertiary)' }}
        whileHover={{ scale: 1.1, color: 'var(--text-primary)' }}
        whileTap={{ scale: 0.9 }}
      >
        {repeat === 'one' ? <RepeatOneIcon size={16} /> : <RepeatIcon size={16} />}
        {repeat !== 'off' && (
          <div
            className="absolute -bottom-0.5 left-1/2 -translate-x-1/2 w-1 h-1 rounded-full"
            style={{ background: 'var(--accent)' }}
          />
        )}
      </motion.button>
    </div>
  )
})
