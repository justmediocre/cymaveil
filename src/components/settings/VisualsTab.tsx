import useVisualSettings from '../../hooks/useVisualSettings'
import useDebouncedSlider from '../../hooks/useDebouncedSlider'
import { SettingToggle, SettingSlider, SettingSelect, SettingSection } from './SettingsControls'
import { useTheme, type ThemePreference } from '../../contexts/ThemeContext'
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

function ImpactBadge({ impact }: { impact: 'High' | 'Medium' | 'Low' }) {
  return (
    <span
      className="text-[10px] uppercase tracking-wider font-medium px-1.5 py-0.5 rounded"
      style={{
        color: impactColor[impact],
        background: impact === 'High' ? 'var(--accent-dim)' : 'var(--bg-elevated)',
      }}
    >
      {impact}
    </span>
  )
}

const FPS_LIMIT_OPTIONS: { value: string; label: string }[] = [
  { value: '0', label: 'Unlimited' },
  { value: '60', label: '60 FPS' },
  { value: '30', label: '30 FPS' },
  { value: '15', label: '15 FPS' },
]

const THEME_OPTIONS: { value: ThemePreference; label: string }[] = [
  { value: 'system', label: 'System' },
  { value: 'dark', label: 'Dark' },
  { value: 'light', label: 'Light' },
]

export default function VisualsTab() {
  const { settings, toggle, setSetting, onBattery } = useVisualSettings()
  const { preference, setPreference } = useTheme()
  const batterySaverActive = settings.disableVisualsOnBattery && onBattery

  const [localOpacity, setLocalOpacity] = useDebouncedSlider(settings.mosaicOpacity, (v) => setSetting('mosaicOpacity', v))
  const [localDensity, setLocalDensity] = useDebouncedSlider(settings.mosaicDensity, (v) => setSetting('mosaicDensity', v))
  const [localMaxTiles, setLocalMaxTiles] = useDebouncedSlider(settings.mosaicMaxTiles, (v) => setSetting('mosaicMaxTiles', v))

  return (
    <>
      <SettingSection title="Appearance">
        <SettingSelect
          label="Theme"
          description="Choose light, dark, or follow your system setting"
          value={preference}
          onChange={(v) => setPreference(v as ThemePreference)}
          options={THEME_OPTIONS}
        />
      </SettingSection>

      <SettingSection title="Effects" className="mt-8">
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

        {VISUAL_TOGGLES.map(({ key, label, description, impact }) => (
          <SettingToggle
            key={key}
            label={label}
            description={description}
            badge={<ImpactBadge impact={impact} />}
            enabled={settings[key] as boolean}
            onToggle={() => toggle(key)}
          />
        ))}

        <SettingToggle
          label="Background Mosaic"
          description="Isometric album art grid"
          badge={<ImpactBadge impact="High" />}
          enabled={settings.backgroundMosaic}
          onToggle={() => toggle('backgroundMosaic')}
        />

        <SettingToggle
          label="Flat mode"
          description={settings.mosaicFlat ? 'Flat 2D grid' : 'Isometric 3D perspective'}
          disabled={!settings.backgroundMosaic}
          enabled={settings.mosaicFlat}
          onToggle={() => toggle('mosaicFlat')}
        />

        <SettingSlider
          label="Opacity"
          description="Background mosaic transparency"
          disabled={!settings.backgroundMosaic}
          value={localOpacity}
          onChange={setLocalOpacity}
          min={0} max={100} step={1}
          format={(v) => `${v}%`}
        />

        <SettingSelect
          label="Mosaic transition"
          description={MOSAIC_TRANSITION_OPTIONS.find(o => o.value === settings.mosaicTransition)?.description ?? ''}
          disabled={!settings.backgroundMosaic}
          value={settings.mosaicTransition}
          onChange={(v) => setSetting('mosaicTransition', v as MosaicTransition)}
          options={MOSAIC_TRANSITION_OPTIONS}
        />

        <SettingSlider
          label="Mosaic density"
          description="Number of tile columns in the background grid"
          disabled={!settings.backgroundMosaic}
          value={localDensity}
          onChange={setLocalDensity}
          min={4} max={14} step={1}
        />

        <SettingSlider
          label="Max tiles"
          description="Cap total rendered tiles to limit memory usage"
          disabled={!settings.backgroundMosaic}
          value={localMaxTiles}
          onChange={setLocalMaxTiles}
          min={24} max={294} step={6}
        />
      </SettingSection>

      <SettingSection title="Power" className="mt-8">
        <SettingToggle
          label="Disable visuals on battery"
          description="Turn off all visual effects when unplugged to save power"
          enabled={settings.disableVisualsOnBattery}
          onToggle={() => toggle('disableVisualsOnBattery')}
        />

        <SettingSelect
          label="Visualizer FPS limit"
          description="Cap the visualizer frame rate to reduce CPU usage"
          value={String(settings.visualizerFpsLimit)}
          onChange={(v) => setSetting('visualizerFpsLimit', Number(v))}
          options={FPS_LIMIT_OPTIONS}
        />

        <SettingToggle
          label="Only limit on battery"
          description="Apply the FPS limit only when running on battery power"
          disabled={settings.visualizerFpsLimit === 0}
          enabled={settings.fpsLimitOnBatteryOnly}
          onToggle={() => toggle('fpsLimitOnBatteryOnly')}
        />
      </SettingSection>
    </>
  )
}
