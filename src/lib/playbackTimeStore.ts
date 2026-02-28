/**
 * Module-level external store for playback currentTime.
 * Decouples high-frequency time updates from React state so only
 * subscribing components (ProgressBar, MiniPlayer) re-render.
 */
let time = 0
const listeners = new Set<() => void>()

export const playbackTimeStore = {
  set(t: number) {
    time = t
    listeners.forEach((l) => l())
  },
  get(): number {
    return time
  },
  subscribe(l: () => void): () => void {
    listeners.add(l)
    return () => listeners.delete(l)
  },
}
