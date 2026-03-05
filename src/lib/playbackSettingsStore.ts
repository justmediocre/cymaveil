/**
 * Module-level external store for playback settings (crossfade duration, etc.).
 * Follows the visualSettingsStore pattern — components subscribe via useSyncExternalStore.
 * Persists to localStorage.
 */

export interface PlaybackSettings {
  crossfadeDuration: number // 0–12 seconds, 0 = disabled
  clickMode: 'classic' | 'queue-building'
}

const STORAGE_KEY = 'cymaveil-playback-settings'

const DEFAULTS: PlaybackSettings = {
  crossfadeDuration: 0,
  clickMode: 'classic',
}

function load(): PlaybackSettings {
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

export const playbackSettingsStore = {
  get(): PlaybackSettings {
    return settings
  },
  set<K extends keyof PlaybackSettings>(key: K, value: PlaybackSettings[K]) {
    settings = { ...settings, [key]: value }
    localStorage.setItem(STORAGE_KEY, JSON.stringify(settings))
    listeners.forEach((l) => l())
  },
  subscribe(l: () => void): () => void {
    listeners.add(l)
    return () => listeners.delete(l)
  },
}
