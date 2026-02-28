/**
 * Unified tick loop — single setTimeout(33ms) ~30fps pub-sub.
 * Components call onTick(cb) which returns an unsubscribe function.
 * Loop self-starts on first subscriber, self-stops when last unsubscribes.
 */

const subscribers = new Set<() => void>()
let timerId: ReturnType<typeof setTimeout> | null = null

function tick() {
  for (const cb of subscribers) cb()
  timerId = setTimeout(tick, 33)
}

export function onTick(cb: () => void): () => void {
  subscribers.add(cb)
  if (subscribers.size === 1) {
    timerId = setTimeout(tick, 33)
  }
  return () => {
    subscribers.delete(cb)
    if (subscribers.size === 0 && timerId !== null) {
      clearTimeout(timerId)
      timerId = null
    }
  }
}
