import { useCallback } from 'react'
import { motion } from 'motion/react'
import { useLibraryCtx } from '../../contexts/library/LibraryContext'
import { FolderIcon } from '../Icons'

export default function LibraryTab() {
  const { folders, isScanning, scanError, scanProgress, importFolder, removeFolder } = useLibraryCtx()

  const handleImportFolder = useCallback(async () => {
    await importFolder()
  }, [importFolder])

  return (
    <section className="max-w-lg mb-8">
      <h2
        className="font-display text-xs font-bold tracking-wider uppercase mb-4"
        style={{ color: 'var(--text-tertiary)' }}
      >
        Library
      </h2>

      <div className="flex flex-col gap-1">
        {folders.length === 0 && !isScanning && (
          <p className="text-sm px-4 py-3" style={{ color: 'var(--text-secondary)' }}>
            No folders added yet. Add a folder to start building your library.
          </p>
        )}

        {folders.map((folder) => (
          <div
            key={folder}
            className="group flex items-center justify-between py-3 px-4 rounded-xl transition-colors hover:[background:var(--bg-hover)]"
          >
            <div className="flex items-center gap-3 min-w-0">
              <FolderIcon size={16} />
              <span
                className="text-sm truncate"
                style={{ color: 'var(--text-primary)' }}
                title={folder}
              >
                {folder}
              </span>
            </div>
            <button
              onClick={() => removeFolder(folder)}
              className="shrink-0 opacity-0 group-hover:opacity-100 transition-opacity flex items-center justify-center w-7 h-7 rounded-lg hover:[color:var(--accent)]"
              style={{ color: 'var(--text-tertiary)' }}
              title="Remove folder"
            >
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <line x1="18" y1="6" x2="6" y2="18" />
                <line x1="6" y1="6" x2="18" y2="18" />
              </svg>
            </button>
          </div>
        ))}

        {isScanning && scanProgress && (
          <div className="px-4 py-2">
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
            <p className="text-[10px] mt-1.5" style={{ color: 'var(--text-tertiary)' }}>
              Scanning {scanProgress.current} / {scanProgress.total} files
            </p>
          </div>
        )}

        {scanError && (
          <p className="text-sm px-4 py-2" style={{ color: 'var(--accent)' }}>
            {scanError}
          </p>
        )}

        <div className="mt-2 px-4">
          <button
            onClick={handleImportFolder}
            disabled={isScanning}
            className="flex items-center gap-2 text-sm py-2 px-3 rounded-lg transition-colors hover:[background:var(--bg-elevated)]"
            style={{
              color: 'var(--accent)',
              background: 'var(--accent-dim)',
              opacity: isScanning ? 0.5 : 1,
            }}
          >
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <line x1="12" y1="5" x2="12" y2="19" />
              <line x1="5" y1="12" x2="19" y2="12" />
            </svg>
            <span>{isScanning ? 'Scanning...' : 'Add Folder'}</span>
          </button>
        </div>
      </div>
    </section>
  )
}
