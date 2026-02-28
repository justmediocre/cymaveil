import { useState, useRef, useCallback, useEffect } from 'react'
import useVisualSettings from '../../hooks/useVisualSettings'
import type { VisualSettings, MosaicTransition } from '../../types'

interface VisualToggle {
  key: keyof VisualSettings
  label: string
  description: string
  impact: 'High' | 'Medium' | 'Low'
}

const VISUAL_TOGGLES: VisualToggle[] = [
  { key: 'canvasVisualizer', label: 'Canvas Visualizer', description: 'Frequency bars on album art', impact: 'Medium' },
  { key: 'glassBlur', label: 'Glass Blur', description: 'Frosted backdrop-filter on panels', impact: 'Medium' },
  { key: 'ambientGlow', label: 'Ambient Glow', description: 'Subtle colored underglow around album art edges', impact: 'Low' },
  { key: 'bassShake', label: 'Bass Hit Zoom', description: 'Album art pulses on strong bass hits', impact: 'Low' },
  { key: 'vinylDisc', label: 'Vinyl Disc', description: 'Spinning vinyl record', impact: 'Low' },
]

const impactColor: Record<'High' | 'Medium' | 'Low', string> = {
  High: 'var(--accent)',
  Medium: 'var(--text-secondary)',
  Low: 'var(--text-tertiary)',
}

const MOSAIC_TRANSITION_OPTIONS: { value: MosaicTransition; label: string; description: string }[] = [
  { value: 'flip', label: '3D Flip', description: 'Card flip with perspective (classic)' },
  { value: 'shrink-grow', label: 'Shrink/Grow', description: 'Scale down and back up with image swap' },
  { value: 'cross-fade', label: 'Cross Fade', description: 'Smooth opacity blend between images' },
  { value: 'fade', label: 'Fade', description: 'Fade to black and reveal new image' },
  { value: 'iris', label: 'Iris', description: 'Circular wipe from center outward' },
  { value: 'random', label: 'Random', description: 'Randomly pick a different animation each time' },
]

function useDebouncedSlider<K extends keyof VisualSettings>(
  storeValue: VisualSettings[K],
  setSetting: (key: K, value: VisualSettings[K]) => void,
  key: K,
  delay = 250,
) {
  const [local, setLocal] = useState(storeValue)
  const timerRef = useRef<ReturnType<typeof setTimeout>>(undefined)

  // Sync local state when store changes externally
  useEffect(() => { setLocal(storeValue) }, [storeValue])

  const onChange = useCallback(
    (value: VisualSettings[K]) => {
      setLocal(value)
      clearTimeout(timerRef.current)
      timerRef.current = setTimeout(() => setSetting(key, value), delay)
    },
    [setSetting, key, delay],
  )

  // Flush on unmount
  useEffect(() => () => clearTimeout(timerRef.current), [])

  return [local, onChange] as const
}

export default function VisualsTab() {
  const { settings, toggle, setSetting, onBattery } = useVisualSettings()
  const batterySaverActive = settings.disableVisualsOnBattery && onBattery

  const [localOpacity, setLocalOpacity] = useDebouncedSlider(settings.mosaicOpacity, setSetting, 'mosaicOpacity')
  const [localDensity, setLocalDensity] = useDebouncedSlider(settings.mosaicDensity, setSetting, 'mosaicDensity')
  const [localMaxTiles, setLocalMaxTiles] = useDebouncedSlider(settings.mosaicMaxTiles, setSetting, 'mosaicMaxTiles')

  return (
    <>
      <section className="max-w-lg">
        <h2
          className="font-display text-xs font-bold tracking-wider uppercase mb-4"
          style={{ color: 'var(--text-tertiary)' }}
        >
          Effects
        </h2>

        {batterySaverActive && (
          <div
            className="flex items-center gap-2 mb-3 px-4 py-2.5 rounded-xl text-xs"
            style={{ background: 'var(--accent-dim)', color: 'var(--accent)' }}
          >
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <rect x="1" y="6" width="18" height="12" rx="2" ry="2" />
              <line x1="23" y1="13" x2="23" y2="11" />
              <line x1="7" y1="10" x2="7" y2="14" />
              <line x1="11" y1="10" x2="11" y2="14" />
            </svg>
            <span>On battery — visuals are paused</span>
          </div>
        )}

        <div className="flex flex-col gap-1">
          {VISUAL_TOGGLES.map(({ key, label, description, impact }) => {
            const enabled = settings[key] as boolean
            return (
              <div
                key={key}
                className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
                style={{ background: 'transparent' }}
                onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
                onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
              >
                <div className="flex flex-col gap-0.5 min-w-0 mr-4">
                  <div className="flex items-center gap-2">
                    <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
                      {label}
                    </span>
                    <span
                      className="text-[10px] uppercase tracking-wider font-medium px-1.5 py-0.5 rounded"
                      style={{
                        color: impactColor[impact],
                        background: impact === 'High' ? 'var(--accent-dim)' : 'var(--bg-elevated)',
                      }}
                    >
                      {impact}
                    </span>
                  </div>
                  <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
                    {description}
                  </span>
                </div>

                <button
                  onClick={() => toggle(key)}
                  className="shrink-0 relative rounded-full transition-colors duration-200"
                  style={{
                    width: 40,
                    height: 22,
                    background: enabled ? 'var(--accent)' : 'var(--bg-elevated)',
                    border: `1px solid ${enabled ? 'var(--accent)' : 'var(--border)'}`,
                  }}
                  aria-label={`Toggle ${label}`}
                  role="switch"
                  aria-checked={enabled}
                >
                  <span
                    className="absolute top-[2px] rounded-full transition-all duration-200"
                    style={{
                      width: 16,
                      height: 16,
                      background: enabled ? '#fff' : 'var(--text-tertiary)',
                      left: enabled ? 20 : 2,
                    }}
                  />
                </button>
              </div>
            )
          })}

          {/* Background Mosaic toggle */}
          <div
            className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
            style={{ background: 'transparent' }}
            onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
            onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
          >
            <div className="flex flex-col gap-0.5 min-w-0 mr-4">
              <div className="flex items-center gap-2">
                <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
                  Background Mosaic
                </span>
                <span
                  className="text-[10px] uppercase tracking-wider font-medium px-1.5 py-0.5 rounded"
                  style={{ color: impactColor['High'], background: 'var(--accent-dim)' }}
                >
                  High
                </span>
              </div>
              <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
                Isometric album art grid
              </span>
            </div>

            <button
              onClick={() => toggle('backgroundMosaic')}
              className="shrink-0 relative rounded-full transition-colors duration-200"
              style={{
                width: 40,
                height: 22,
                background: settings.backgroundMosaic ? 'var(--accent)' : 'var(--bg-elevated)',
                border: `1px solid ${settings.backgroundMosaic ? 'var(--accent)' : 'var(--border)'}`,
              }}
              aria-label="Toggle Background Mosaic"
              role="switch"
              aria-checked={settings.backgroundMosaic}
            >
              <span
                className="absolute top-[2px] rounded-full transition-all duration-200"
                style={{
                  width: 16,
                  height: 16,
                  background: settings.backgroundMosaic ? '#fff' : 'var(--text-tertiary)',
                  left: settings.backgroundMosaic ? 20 : 2,
                }}
              />
            </button>
          </div>

          {/* Mosaic flat/2D toggle */}
          <div
            className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
            style={{
              background: 'transparent',
              opacity: settings.backgroundMosaic ? 1 : 0.5,
              pointerEvents: settings.backgroundMosaic ? 'auto' : 'none',
            }}
            onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
            onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
          >
            <div className="flex flex-col gap-0.5 min-w-0 mr-4">
              <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
                Flat mode
              </span>
              <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
                {settings.mosaicFlat ? 'Flat 2D grid' : 'Isometric 3D perspective'}
              </span>
            </div>

            <button
              onClick={() => toggle('mosaicFlat')}
              className="shrink-0 relative rounded-full transition-colors duration-200"
              style={{
                width: 40,
                height: 22,
                background: settings.mosaicFlat ? 'var(--accent)' : 'var(--bg-elevated)',
                border: `1px solid ${settings.mosaicFlat ? 'var(--accent)' : 'var(--border)'}`,
              }}
              aria-label="Toggle flat mosaic mode"
              role="switch"
              aria-checked={settings.mosaicFlat}
            >
              <span
                className="absolute top-[2px] rounded-full transition-all duration-200"
                style={{
                  width: 16,
                  height: 16,
                  background: settings.mosaicFlat ? '#fff' : 'var(--text-tertiary)',
                  left: settings.mosaicFlat ? 20 : 2,
                }}
              />
            </button>
          </div>

          {/* Mosaic opacity slider */}
          <div
            className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
            style={{
              background: 'transparent',
              opacity: settings.backgroundMosaic ? 1 : 0.5,
              pointerEvents: settings.backgroundMosaic ? 'auto' : 'none',
            }}
            onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
            onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
          >
            <div className="flex flex-col gap-0.5 min-w-0 mr-4">
              <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
                Opacity
              </span>
              <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
                Background mosaic transparency
              </span>
            </div>

            <div className="shrink-0 flex items-center gap-2">
              <input
                type="range"
                min={0}
                max={100}
                step={1}
                value={localOpacity}
                onChange={(e) => setLocalOpacity(Number(e.target.value))}
                className="w-24 accent-[var(--accent)]"
                style={{ cursor: 'pointer' }}
              />
              <span
                className="text-xs tabular-nums w-7 text-right"
                style={{ color: 'var(--text-secondary)', fontFamily: 'var(--font-mono)' }}
              >
                {localOpacity}%
              </span>
            </div>
          </div>

          {/* Mosaic transition style dropdown */}
          <div
            className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
            style={{
              background: 'transparent',
              opacity: settings.backgroundMosaic ? 1 : 0.5,
              pointerEvents: settings.backgroundMosaic ? 'auto' : 'none',
            }}
            onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
            onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
          >
            <div className="flex flex-col gap-0.5 min-w-0 mr-4">
              <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
                Mosaic transition
              </span>
              <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
                {MOSAIC_TRANSITION_OPTIONS.find(o => o.value === settings.mosaicTransition)?.description ?? ''}
              </span>
            </div>

            <select
              value={settings.mosaicTransition}
              onChange={(e) => setSetting('mosaicTransition', e.target.value as MosaicTransition)}
              className="shrink-0 text-sm rounded-lg px-3 py-1.5 cursor-pointer"
              style={{
                background: 'var(--bg-elevated)',
                color: 'var(--text-primary)',
                border: '1px solid var(--border)',
              }}
            >
              {MOSAIC_TRANSITION_OPTIONS.map(opt => (
                <option key={opt.value} value={opt.value}>
                  {opt.label}
                </option>
              ))}
            </select>
          </div>

          {/* Mosaic density slider */}
          <div
            className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
            style={{
              background: 'transparent',
              opacity: settings.backgroundMosaic ? 1 : 0.5,
              pointerEvents: settings.backgroundMosaic ? 'auto' : 'none',
            }}
            onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
            onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
          >
            <div className="flex flex-col gap-0.5 min-w-0 mr-4">
              <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
                Mosaic density
              </span>
              <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
                Number of tile columns in the background grid
              </span>
            </div>

            <div className="shrink-0 flex items-center gap-2">
              <input
                type="range"
                min={4}
                max={14}
                step={1}
                value={localDensity}
                onChange={(e) => setLocalDensity(Number(e.target.value))}
                className="w-24 accent-[var(--accent)]"
                style={{ cursor: 'pointer' }}
              />
              <span
                className="text-xs tabular-nums w-7 text-right"
                style={{ color: 'var(--text-secondary)', fontFamily: 'var(--font-mono)' }}
              >
                {localDensity}
              </span>
            </div>
          </div>

          {/* Mosaic max tiles slider */}
          <div
            className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
            style={{
              background: 'transparent',
              opacity: settings.backgroundMosaic ? 1 : 0.5,
              pointerEvents: settings.backgroundMosaic ? 'auto' : 'none',
            }}
            onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
            onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
          >
            <div className="flex flex-col gap-0.5 min-w-0 mr-4">
              <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
                Max tiles
              </span>
              <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
                Cap total rendered tiles to limit memory usage
              </span>
            </div>

            <div className="shrink-0 flex items-center gap-2">
              <input
                type="range"
                min={24}
                max={294}
                step={6}
                value={localMaxTiles}
                onChange={(e) => setLocalMaxTiles(Number(e.target.value))}
                className="w-24 accent-[var(--accent)]"
                style={{ cursor: 'pointer' }}
              />
              <span
                className="text-xs tabular-nums w-7 text-right"
                style={{ color: 'var(--text-secondary)', fontFamily: 'var(--font-mono)' }}
              >
                {localMaxTiles}
              </span>
            </div>
          </div>
        </div>
      </section>

      {/* Power section */}
      <section className="max-w-lg mt-8">
        <h2
          className="font-display text-xs font-bold tracking-wider uppercase mb-4"
          style={{ color: 'var(--text-tertiary)' }}
        >
          Power
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
                Disable visuals on battery
              </span>
              <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
                Turn off all visual effects when unplugged to save power
              </span>
            </div>

            <button
              onClick={() => toggle('disableVisualsOnBattery')}
              className="shrink-0 relative rounded-full transition-colors duration-200"
              style={{
                width: 40,
                height: 22,
                background: settings.disableVisualsOnBattery ? 'var(--accent)' : 'var(--bg-elevated)',
                border: `1px solid ${settings.disableVisualsOnBattery ? 'var(--accent)' : 'var(--border)'}`,
              }}
              aria-label="Toggle disable visuals on battery"
              role="switch"
              aria-checked={settings.disableVisualsOnBattery}
            >
              <span
                className="absolute top-[2px] rounded-full transition-all duration-200"
                style={{
                  width: 16,
                  height: 16,
                  background: settings.disableVisualsOnBattery ? '#fff' : 'var(--text-tertiary)',
                  left: settings.disableVisualsOnBattery ? 20 : 2,
                }}
              />
            </button>
          </div>
        </div>
      </section>
    </>
  )
}
