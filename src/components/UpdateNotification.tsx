import { motion, AnimatePresence } from 'motion/react'
import type { UpdateInfo } from '../types'

interface Props {
  info: UpdateInfo | null
  onDismiss: (version: string) => void
  onOpenRelease: (url: string) => void
}

export default function UpdateNotification({ info, onDismiss, onOpenRelease }: Props) {
  return (
    <AnimatePresence>
      {info && (
        <motion.div
          initial={{ opacity: 0, y: 20, scale: 0.95 }}
          animate={{ opacity: 1, y: 0, scale: 1 }}
          exit={{ opacity: 0, y: 20, scale: 0.95 }}
          transition={{ type: 'spring', stiffness: 400, damping: 30 }}
          style={{
            position: 'fixed',
            bottom: '5.5rem',
            right: '1.25rem',
            zIndex: 100,
            maxWidth: '20rem',
            borderRadius: '0.75rem',
            background: 'var(--bg-elevated)',
            border: '1px solid var(--border-subtle)',
            padding: '0.875rem 1rem',
            boxShadow: '0 8px 32px rgba(0,0,0,0.4)',
          }}
        >
          {/* Header row */}
          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: '0.375rem' }}>
            <span style={{ fontSize: '0.8125rem', fontWeight: 600, color: 'var(--text-primary)' }}>
              {info.releaseName}
            </span>
            <button
              onClick={() => onDismiss(info.version)}
              aria-label="Dismiss"
              style={{
                background: 'none',
                border: 'none',
                cursor: 'pointer',
                color: 'var(--text-tertiary)',
                padding: '0.125rem',
                marginRight: '-0.25rem',
                lineHeight: 1,
                fontSize: '1rem',
              }}
            >
              &#x2715;
            </button>
          </div>

          {/* Version badge */}
          <span
            style={{
              display: 'inline-block',
              fontSize: '0.625rem',
              textTransform: 'uppercase',
              letterSpacing: '0.05em',
              fontWeight: 500,
              padding: '0.125rem 0.375rem',
              borderRadius: '0.25rem',
              color: 'var(--accent, #60a5fa)',
              background: 'rgba(96,165,250,0.1)',
              marginBottom: '0.5rem',
            }}
          >
            v{info.version} available
          </span>

          {/* Truncated release notes */}
          {info.releaseNotes && (
            <p
              style={{
                fontSize: '0.75rem',
                color: 'var(--text-secondary)',
                lineHeight: 1.4,
                marginBottom: '0.625rem',
                display: '-webkit-box',
                WebkitLineClamp: 3,
                WebkitBoxOrient: 'vertical',
                overflow: 'hidden',
              }}
            >
              {info.releaseNotes}
            </p>
          )}

          {/* Action button */}
          <button
            onClick={() => onOpenRelease(info.releaseUrl)}
            style={{
              width: '100%',
              padding: '0.4rem 0',
              borderRadius: '0.5rem',
              border: 'none',
              cursor: 'pointer',
              fontSize: '0.75rem',
              fontWeight: 500,
              color: 'var(--text-primary)',
              background: 'rgba(255,255,255,0.08)',
            }}
          >
            View release
          </button>
        </motion.div>
      )}
    </AnimatePresence>
  )
}
