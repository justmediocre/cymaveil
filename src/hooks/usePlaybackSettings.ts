import { useCallback, useSyncExternalStore } from 'react'
import type { PlaybackSettings } from '../lib/playbackSettingsStore'
import { playbackSettingsStore } from '../lib/playbackSettingsStore'

export default function usePlaybackSettings() {
  const settings = useSyncExternalStore(playbackSettingsStore.subscribe, playbackSettingsStore.get)

  const setSetting = useCallback(<K extends keyof PlaybackSettings>(key: K, value: PlaybackSettings[K]) => {
    playbackSettingsStore.set(key, value)
  }, [])

  return { settings, setSetting }
}
