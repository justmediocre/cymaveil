import { useState, useEffect, useCallback } from 'react'
import useUpdateChecker from '../../hooks/useUpdateChecker'
import type { UpdateInfo } from '../../types'

const LICENSES = [
  {
    name: 'Outfit',
    license: 'SIL Open Font License 1.1',
    copyright: 'Copyright 2021 The Outfit Project Authors (https://github.com/Outfitio/Outfit-Fonts)',
  },
  {
    name: 'JetBrains Mono',
    license: 'SIL Open Font License 1.1',
    copyright: 'Copyright 2020 The JetBrains Mono Project Authors (https://github.com/JetBrains/JetBrainsMono)',
  },
  {
    name: 'Bricolage Grotesque',
    license: 'SIL Open Font License 1.1',
    copyright: 'Copyright 2022 The Bricolage Grotesque Project Authors (https://github.com/ateliertriay/bricolage)',
  },
  {
    name: 'Depth Anything v2',
    license: 'Apache License 2.0',
    copyright: 'Copyright 2024 Depth Anything v2 Authors (https://github.com/DepthAnything/Depth-Anything-V2)',
  },
  {
    name: 'React',
    license: 'MIT',
    copyright: 'Copyright (c) Meta Platforms, Inc. and affiliates',
  },
  {
    name: 'Electron',
    license: 'MIT',
    copyright: 'Copyright (c) Electron contributors, Copyright (c) GitHub Inc.',
  },
  {
    name: 'Motion (Framer Motion)',
    license: 'MIT',
    copyright: 'Copyright (c) 2018 Framer B.V.',
  },
  {
    name: 'Hugging Face Transformers.js',
    license: 'Apache License 2.0',
    copyright: 'Copyright 2023 Xenova, Copyright 2024 Hugging Face',
  },
  {
    name: 'music-metadata',
    license: 'MIT',
    copyright: 'Copyright (c) 2017 Borewit',
  },
]

export default function AboutTab() {
  const { updateInfo, checkNow, openRelease } = useUpdateChecker()
  const [checking, setChecking] = useState(false)
  const [checkResult, setCheckResult] = useState<UpdateInfo | null | 'none'>(null)
  const [autoCheck, setAutoCheck] = useState(true)

  // Load the persisted auto-check preference
  useEffect(() => {
    window.electronAPI?.getUpdateCheckEnabled().then(setAutoCheck)
  }, [])

  const handleCheck = useCallback(async () => {
    setChecking(true)
    setCheckResult(null)
    const info = await checkNow()
    setCheckResult(info ?? 'none')
    setChecking(false)
  }, [checkNow])

  const handleToggleAutoCheck = useCallback((enabled: boolean) => {
    setAutoCheck(enabled)
    window.electronAPI?.setUpdateCheckEnabled(enabled)
  }, [])

  const latestUpdate = updateInfo ?? (checkResult !== 'none' ? checkResult : null)

  return (
    <>
      {/* App info */}
      <section className="max-w-lg">
        <div className="flex items-center gap-4 mb-6">
          <div>
            <div className="flex items-center gap-2">
              <h2 className="font-display text-xl font-bold" style={{ color: 'var(--text-primary)' }}>
                Cymaveil
              </h2>
              {latestUpdate && (
                <span
                  style={{
                    fontSize: '0.625rem',
                    textTransform: 'uppercase',
                    letterSpacing: '0.05em',
                    fontWeight: 500,
                    padding: '0.125rem 0.375rem',
                    borderRadius: '0.25rem',
                    color: 'var(--accent, #60a5fa)',
                    background: 'rgba(96,165,250,0.1)',
                  }}
                >
                  v{latestUpdate.version} available
                </span>
              )}
            </div>
            <p className="text-xs mt-0.5" style={{ color: 'var(--text-secondary)' }}>
              Version {__APP_VERSION__}
            </p>
          </div>
        </div>

        <p className="text-sm leading-relaxed mb-1" style={{ color: 'var(--text-secondary)' }}>
          A refined music player with living album art.
        </p>
        <p className="text-xs" style={{ color: 'var(--text-tertiary)' }}>
          Copyright (c) 2026 Cymaveil. Released under the MIT License.
        </p>

        {/* Update controls */}
        <div className="mt-4 flex flex-col gap-3">
          <div className="flex items-center gap-3">
            <button
              onClick={handleCheck}
              disabled={checking}
              style={{
                padding: '0.375rem 0.75rem',
                borderRadius: '0.5rem',
                border: 'none',
                cursor: checking ? 'default' : 'pointer',
                fontSize: '0.75rem',
                fontWeight: 500,
                color: 'var(--text-primary)',
                background: 'rgba(255,255,255,0.08)',
                opacity: checking ? 0.5 : 1,
              }}
            >
              {checking ? 'Checking...' : 'Check for updates'}
            </button>
            {latestUpdate && (
              <button
                onClick={() => openRelease(latestUpdate.releaseUrl)}
                style={{
                  padding: '0.375rem 0.75rem',
                  borderRadius: '0.5rem',
                  border: 'none',
                  cursor: 'pointer',
                  fontSize: '0.75rem',
                  fontWeight: 500,
                  color: 'var(--accent, #60a5fa)',
                  background: 'rgba(96,165,250,0.08)',
                }}
              >
                View release
              </button>
            )}
            {checkResult === 'none' && !latestUpdate && (
              <span className="text-xs" style={{ color: 'var(--text-tertiary)' }}>
                You're up to date
              </span>
            )}
          </div>
          <label
            className="flex items-center gap-2 text-xs cursor-pointer select-none"
            style={{ color: 'var(--text-secondary)' }}
          >
            <input
              type="checkbox"
              checked={autoCheck}
              onChange={(e) => handleToggleAutoCheck(e.target.checked)}
              style={{ accentColor: 'var(--accent, #60a5fa)' }}
            />
            Check for updates automatically
          </label>
        </div>
      </section>

      {/* Third-party notices */}
      <section className="max-w-lg mt-8">
        <h2
          className="font-display text-xs font-bold tracking-wider uppercase mb-4"
          style={{ color: 'var(--text-tertiary)' }}
        >
          Third-Party Notices
        </h2>

        <p className="text-xs mb-4" style={{ color: 'var(--text-secondary)' }}>
          Cymaveil is built with the following open-source software and assets.
        </p>

        <div className="flex flex-col gap-1">
          {LICENSES.map(({ name, license, copyright }) => (
            <div
              key={name}
              className="py-3 px-4 rounded-xl transition-colors"
              style={{ background: 'transparent' }}
              onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
              onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
            >
              <div className="flex items-center gap-2 mb-0.5">
                <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
                  {name}
                </span>
                <span
                  className="text-[10px] uppercase tracking-wider font-medium px-1.5 py-0.5 rounded"
                  style={{ color: 'var(--text-tertiary)', background: 'var(--bg-elevated)' }}
                >
                  {license}
                </span>
              </div>
              <span className="text-xs" style={{ color: 'var(--text-tertiary)' }}>
                {copyright}
              </span>
            </div>
          ))}
        </div>
      </section>
    </>
  )
}
