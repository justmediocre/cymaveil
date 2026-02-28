import { useCallback, useMemo, useSyncExternalStore } from 'react'
import type { VisualSettings } from '../types'
import { visualSettingsStore } from '../lib/visualSettingsStore'
import { useBatteryOnBattery } from './useBatteryStatus'

/** Boolean visual keys that get disabled on battery */
const VISUAL_KEYS: (keyof VisualSettings)[] = [
  'canvasVisualizer',
  'ambientGlow',
  'bassShake',
  'vinylDisc',
  'backgroundMosaic',
  'glassBlur',
]

export default function useVisualSettings() {
  const stored = useSyncExternalStore(visualSettingsStore.subscribe, visualSettingsStore.get)
  const onBattery = useBatteryOnBattery()

  const settings = useMemo(() => {
    if (!stored.disableVisualsOnBattery || !onBattery) return stored
    // Override only boolean visual keys to false; non-visual / enum settings stay intact
    const overridden = { ...stored }
    for (const key of VISUAL_KEYS) overridden[key] = false as never
    return overridden
  }, [stored, onBattery])

  const toggle = useCallback((key: keyof VisualSettings) => {
    visualSettingsStore.set(key, !visualSettingsStore.get()[key] as never)
  }, [])

  const setSetting = useCallback(<K extends keyof VisualSettings>(key: K, value: VisualSettings[K]) => {
    visualSettingsStore.set(key, value)
  }, [])

  return { settings, toggle, setSetting, onBattery }
}
