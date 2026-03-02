import usePlaybackSettings from '../../hooks/usePlaybackSettings'
import useDebouncedSlider from '../../hooks/useDebouncedSlider'
import { SettingSlider, SettingSection } from './SettingsControls'

export default function PlaybackTab() {
  const { settings, setSetting } = usePlaybackSettings()
  const [localCrossfade, setLocalCrossfade] = useDebouncedSlider(
    settings.crossfadeDuration,
    (v) => setSetting('crossfadeDuration', v),
  )

  return (
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
  )
}
