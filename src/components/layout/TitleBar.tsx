import { motion, AnimatePresence } from 'motion/react'
import ThemeToggle from '../ThemeToggle'
import { SidebarIcon, ListIcon, ExpandIcon, ShrinkIcon } from '../Icons'
import type { BatchProgress } from '../../hooks/useBatchSegmentation'

interface TitleBarProps {
  sidebarOpen: boolean
  onToggleSidebar: () => void
  showQueue: boolean
  onToggleQueue: () => void
  isFullscreen: boolean
  onToggleFullscreen: () => void
  batchProgress?: { processing: boolean; progress: BatchProgress | null }
}

export default function TitleBar({
  sidebarOpen,
  onToggleSidebar,
  showQueue,
  onToggleQueue,
  isFullscreen,
  onToggleFullscreen,
  batchProgress,
}: TitleBarProps) {
  return (
    <div className="drag-region flex items-center justify-between px-4 pt-3 pb-2 shrink-0">
      <div className="flex items-center gap-2">
        {/* macOS traffic light offset */}
        {!sidebarOpen && <div className="w-16" />}
        <motion.button
          onClick={onToggleSidebar}
          className="no-drag flex items-center justify-center w-8 h-8 rounded-lg"
          aria-label={sidebarOpen ? 'Close sidebar' : 'Open sidebar'}
          style={{ color: 'var(--text-tertiary)' }}
          whileHover={{ scale: 1.05, color: 'var(--text-primary)' }}
          whileTap={{ scale: 0.95 }}
          transition={{ duration: 0.15, ease: 'easeOut' }}
        >
          <SidebarIcon size={17} />
        </motion.button>

        {/* Batch processing indicator */}
        <AnimatePresence>
          {batchProgress?.processing && batchProgress.progress && (
            <motion.div
              initial={{ opacity: 0, x: -8 }}
              animate={{ opacity: 1, x: 0 }}
              exit={{ opacity: 0, x: -8 }}
              transition={{ duration: 0.2 }}
              className="flex items-center gap-1.5 ml-1"
              style={{ color: 'var(--text-tertiary)' }}
            >
              <svg
                width="14"
                height="14"
                viewBox="0 0 14 14"
                fill="none"
                className="animate-spin"
              >
                <circle cx="7" cy="7" r="5.5" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeDasharray="20 14" />
              </svg>
              <span className="text-xs whitespace-nowrap">
                Processing art {batchProgress.progress.current}/{batchProgress.progress.total}
              </span>
            </motion.div>
          )}
        </AnimatePresence>
      </div>

      <div className="flex items-center gap-1">
        <motion.button
          onClick={onToggleQueue}
          className="no-drag flex items-center justify-center w-8 h-8 rounded-lg"
          aria-label={showQueue ? 'Close queue' : 'Open queue'}
          style={{ color: showQueue ? 'var(--accent)' : 'var(--text-tertiary)' }}
          whileHover={{ scale: 1.05, color: 'var(--text-primary)' }}
          whileTap={{ scale: 0.95 }}
          transition={{ duration: 0.15, ease: 'easeOut' }}
        >
          <ListIcon size={17} />
        </motion.button>
        <motion.button
          onClick={onToggleFullscreen}
          className="no-drag flex items-center justify-center w-8 h-8 rounded-lg"
          aria-label={isFullscreen ? 'Exit fullscreen' : 'Enter fullscreen'}
          style={{ color: 'var(--text-tertiary)' }}
          whileHover={{ scale: 1.05, color: 'var(--text-primary)' }}
          whileTap={{ scale: 0.95 }}
          transition={{ duration: 0.15, ease: 'easeOut' }}
        >
          {isFullscreen ? <ShrinkIcon size={15} /> : <ExpandIcon size={15} />}
        </motion.button>
        <ThemeToggle />

        {/* Windows title bar buttons */}
        {window.electronAPI?.platform === 'win32' && (
          <div className="flex items-center ml-2">
            <button
              onClick={() => window.electronAPI!.windowMinimize()}
              className="no-drag flex items-center justify-center w-10 h-8 transition-colors hover:bg-white/10"
              style={{ color: 'var(--text-secondary)' }}
              aria-label="Minimize"
            >
              <svg width="10" height="1" viewBox="0 0 10 1"><rect width="10" height="1" fill="currentColor"/></svg>
            </button>
            <button
              onClick={() => window.electronAPI!.windowMaximize()}
              className="no-drag flex items-center justify-center w-10 h-8 transition-colors hover:bg-white/10"
              style={{ color: 'var(--text-secondary)' }}
              aria-label="Maximize"
            >
              <svg width="10" height="10" viewBox="0 0 10 10" fill="none" stroke="currentColor" strokeWidth="1"><rect x="0.5" y="0.5" width="9" height="9"/></svg>
            </button>
            <button
              onClick={() => window.electronAPI!.windowClose()}
              className="no-drag flex items-center justify-center w-10 h-8 transition-colors hover:bg-red-500/80 hover:text-white"
              style={{ color: 'var(--text-secondary)' }}
              aria-label="Close"
            >
              <svg width="10" height="10" viewBox="0 0 10 10" stroke="currentColor" strokeWidth="1.2"><line x1="0" y1="0" x2="10" y2="10"/><line x1="10" y1="0" x2="0" y2="10"/></svg>
            </button>
          </div>
        )}
      </div>
    </div>
  )
}
