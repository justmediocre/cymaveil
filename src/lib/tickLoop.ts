/**
 * Unified tick loop — single requestAnimationFrame pub-sub.
 * Syncs with the display refresh rate for jitter-free animation.
 * Components call onTick(cb) which returns an unsubscribe function.
 * Loop self-starts on first subscriber, self-stops when last unsubscribes.
 */

const subscribers = new Set<() => void>()
let rafId: number | null = null

function tick() {
  for (const cb of subscribers) cb()
  rafId = requestAnimationFrame(tick)
}

export function onTick(cb: () => void): () => void {
  subscribers.add(cb)
  if (subscribers.size === 1) {
    rafId = requestAnimationFrame(tick)
  }
  return () => {
    subscribers.delete(cb)
    if (subscribers.size === 0 && rafId !== null) {
      cancelAnimationFrame(rafId)
      rafId = null
    }
  }
}
