import type { VisualSettings } from '../types'

/**
 * Module-level external store for visual effect toggles.
 * Matches the playbackTimeStore pattern — components subscribe via useSyncExternalStore.
 * Persists to localStorage. Settings change rarely (only on toggle).
 */
const STORAGE_KEY = 'cymaveil-visual-settings'

const DEFAULTS: VisualSettings = {
  canvasVisualizer: true,
  ambientGlow: true,
  bassShake: true,
  vinylDisc: true,
  backgroundMosaic: true,
  mosaicTransition: 'random',
  mosaicDensity: 8,
  mosaicMaxTiles: 120,
  mosaicOpacity: 18,
  mosaicFlat: false,
  glassBlur: true,
  disableVisualsOnBattery: false,
  depthLayerEnabled: false,
  segmentationBackend: 'depth-anything',
  visualizerStyle: 'full-surface',
  visualizerIntensity: 65,
  visualizerColorMode: 'auto',
  visualizerCustomColor: '#00ffff',
  maskDefaults: {},
  maskCacheVersion: 0,
}

function load(): VisualSettings {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    if (raw) {
      return { ...DEFAULTS, ...JSON.parse(raw) }
    }
  } catch {}
  return { ...DEFAULTS }
}

let settings = load()
const listeners = new Set<() => void>()

function syncGlassBlur() {
  if (settings.glassBlur) {
    document.documentElement.removeAttribute('data-no-glass-blur')
  } else {
    document.documentElement.setAttribute('data-no-glass-blur', '')
  }
}

// Apply on init
syncGlassBlur()

export const visualSettingsStore = {
  get(): VisualSettings {
    return settings
  },
  set<K extends keyof VisualSettings>(key: K, value: VisualSettings[K]) {
    settings = { ...settings, [key]: value }
    localStorage.setItem(STORAGE_KEY, JSON.stringify(settings))
    syncGlassBlur()
    listeners.forEach((l) => l())
  },
  setBulk(updates: Partial<VisualSettings>) {
    settings = { ...settings, ...updates }
    localStorage.setItem(STORAGE_KEY, JSON.stringify(settings))
    syncGlassBlur()
    listeners.forEach((l) => l())
  },
  subscribe(l: () => void): () => void {
    listeners.add(l)
    return () => listeners.delete(l)
  },
}
