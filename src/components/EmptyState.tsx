import { motion } from 'motion/react'
import { useLibraryCtx } from '../contexts/library/LibraryContext'
import { FolderIcon, DiscIcon, MusicNoteIcon } from './Icons'

const icons = {
  folder: FolderIcon,
  disc: DiscIcon,
  music: MusicNoteIcon,
} as const

interface EmptyStateProps {
  icon?: keyof typeof icons
  title: string
  subtitle: string
  showImport?: boolean
}

export default function EmptyState({ icon = 'disc', title, subtitle, showImport = false }: EmptyStateProps) {
  const { importFolder, isScanning, scanProgress, scanError } = useLibraryCtx()
  const Icon = icons[icon]

  return (
    <motion.div
      className="h-full flex flex-col items-center justify-center gap-4 px-6"
      initial={{ opacity: 0, y: 12 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ duration: 0.35, ease: 'easeOut' }}
    >
      <div
        className="flex items-center justify-center rounded-2xl"
        style={{
          width: 64,
          height: 64,
          background: 'var(--accent-dim)',
          color: 'var(--accent)',
        }}
      >
        <Icon size={32} />
      </div>

      <div className="text-center">
        <p className="text-base font-semibold" style={{ color: 'var(--text-primary)' }}>
          {title}
        </p>
        <p className="text-sm mt-1" style={{ color: 'var(--text-secondary)' }}>
          {subtitle}
        </p>
      </div>

      {showImport && (
        <>
          <motion.button
            onClick={() => importFolder()}
            disabled={isScanning}
            className="no-drag flex items-center gap-2 px-5 py-2.5 rounded-full text-sm font-medium mt-2"
            style={{
              background: 'var(--accent)',
              color: '#fff',
              opacity: isScanning ? 0.5 : 1,
            }}
            whileHover={isScanning ? undefined : { scale: 1.05 }}
            whileTap={isScanning ? undefined : { scale: 0.95 }}
          >
            <FolderIcon size={16} />
            {isScanning ? 'Scanning...' : 'Add Music Folder'}
          </motion.button>

          {isScanning && scanProgress && (
            <div className="w-full max-w-[220px] flex flex-col items-center gap-1.5">
              <div
                className="w-full rounded-full overflow-hidden"
                style={{ height: 3, background: 'var(--border-subtle)' }}
              >
                <motion.div
                  className="h-full rounded-full"
                  style={{ background: 'var(--accent)' }}
                  initial={{ width: 0 }}
                  animate={{ width: `${scanProgress.total ? (scanProgress.current / scanProgress.total) * 100 : 0}%` }}
                  transition={{ duration: 0.3, ease: 'easeOut' }}
                />
              </div>
              <p className="text-[11px]" style={{ color: 'var(--text-tertiary)' }}>
                Scanning {scanProgress.current} / {scanProgress.total} files
              </p>
            </div>
          )}

          {scanError && (
            <p className="text-sm" style={{ color: 'var(--accent)' }}>
              {scanError}
            </p>
          )}
        </>
      )}
    </motion.div>
  )
}
