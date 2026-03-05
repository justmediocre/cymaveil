import usePlaybackSettings from '../../hooks/usePlaybackSettings'
import useDebouncedSlider from '../../hooks/useDebouncedSlider'
import { SettingSlider, SettingSection, SettingSelect } from './SettingsControls'

const CLICK_MODE_OPTIONS = [
  { value: 'classic', label: 'Classic (play immediately)' },
  { value: 'queue-building', label: 'Queue building (add to Now Playing)' },
]

export default function PlaybackTab() {
  const { settings, setSetting } = usePlaybackSettings()
  const [localCrossfade, setLocalCrossfade] = useDebouncedSlider(
    settings.crossfadeDuration,
    (v) => setSetting('crossfadeDuration', v),
  )

  return (
    <>
      <SettingSection title="Click behavior">
        <SettingSelect
          label="Track click action"
          description="Classic plays immediately on click. Queue building adds to Now Playing on click, double-click to play."
          value={settings.clickMode}
          onChange={(v) => setSetting('clickMode', v as 'classic' | 'queue-building')}
          options={CLICK_MODE_OPTIONS}
        />
      </SettingSection>
      <SettingSection title="Crossfade">
        <SettingSlider
          label="Crossfade duration"
          description="Smoothly blend audio when tracks advance naturally"
          value={localCrossfade}
          onChange={setLocalCrossfade}
          min={0} max={12} step={1}
          format={(v) => v === 0 ? 'Off' : `${v}s`}
        />
      </SettingSection>
    </>
  )
}
