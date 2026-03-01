import { useState, useRef, useCallback, useEffect } from 'react'
import usePlaybackSettings from '../../hooks/usePlaybackSettings'
import type { PlaybackSettings } from '../../lib/playbackSettingsStore'

function useDebouncedSlider<K extends keyof PlaybackSettings>(
  storeValue: PlaybackSettings[K],
  setSetting: (key: K, value: PlaybackSettings[K]) => void,
  key: K,
  delay = 250,
) {
  const [local, setLocal] = useState(storeValue)
  const timerRef = useRef<ReturnType<typeof setTimeout>>(undefined)

  useEffect(() => { setLocal(storeValue) }, [storeValue])

  const onChange = useCallback(
    (value: PlaybackSettings[K]) => {
      setLocal(value)
      clearTimeout(timerRef.current)
      timerRef.current = setTimeout(() => setSetting(key, value), delay)
    },
    [setSetting, key, delay],
  )

  useEffect(() => () => clearTimeout(timerRef.current), [])

  return [local, onChange] as const
}

export default function PlaybackTab() {
  const { settings, setSetting } = usePlaybackSettings()
  const [localCrossfade, setLocalCrossfade] = useDebouncedSlider(settings.crossfadeDuration, setSetting, 'crossfadeDuration')

  return (
    <section className="max-w-lg">
      <h2
        className="font-display text-xs font-bold tracking-wider uppercase mb-4"
        style={{ color: 'var(--text-tertiary)' }}
      >
        Crossfade
      </h2>

      <div className="flex flex-col gap-1">
        <div
          className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
          style={{ background: 'transparent' }}
          onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
          onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
        >
          <div className="flex flex-col gap-0.5 min-w-0 mr-4">
            <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
              Crossfade duration
            </span>
            <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
              Smoothly blend audio when tracks advance naturally
            </span>
          </div>

          <div className="shrink-0 flex items-center gap-2">
            <input
              type="range"
              min={0}
              max={12}
              step={1}
              value={localCrossfade}
              onChange={(e) => setLocalCrossfade(Number(e.target.value))}
              className="w-24 accent-[var(--accent)]"
              style={{ cursor: 'pointer' }}
            />
            <span
              className="text-xs tabular-nums w-7 text-right"
              style={{ color: 'var(--text-secondary)', fontFamily: 'var(--font-mono)' }}
            >
              {localCrossfade === 0 ? 'Off' : `${localCrossfade}s`}
            </span>
          </div>
        </div>
      </div>
    </section>
  )
}
