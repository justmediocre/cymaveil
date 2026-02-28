/**
 * Performance collector — all code gated behind __PERF_HUD__.
 * When the flag is false, exports are noops that V8 inlines away.
 * When enabled, uses ring buffers (Float64Array) for zero GC pressure.
 */

import type { PerfSnapshot } from '../types'

interface RingChannel {
  samples: Float64Array
  head: number
  count: number
}

interface RenderCounter {
  count: number
  lastReset: number
  rps: number
}

// Noop exports for production — V8 inlines these away
let perfMarkStart: (channelName: string) => () => void = () => () => {}
let perfCountRender: (name: string) => void = () => {}
let perfSubscribe: (listener: (snap: PerfSnapshot) => void) => () => void = () => () => {}

if (__PERF_HUD__) {
  const RING_SIZE = 120 // ~4-8s of samples depending on channel rate

  const channels = new Map<string, RingChannel>()
  const renderCounters = new Map<string, RenderCounter>()

  const listeners = new Set<(snap: PerfSnapshot) => void>()
  let emitTimer: ReturnType<typeof setInterval> | null = null

  function getOrCreateChannel(name: string): RingChannel {
    let ch = channels.get(name)
    if (!ch) {
      ch = { samples: new Float64Array(RING_SIZE), head: 0, count: 0 }
      channels.set(name, ch)
    }
    return ch
  }

  function pushSample(ch: RingChannel, value: number) {
    ch.samples[ch.head] = value
    ch.head = (ch.head + 1) % RING_SIZE
    if (ch.count < RING_SIZE) ch.count++
  }

  function channelStats(ch: RingChannel): { avg: number; max: number } {
    if (ch.count === 0) return { avg: 0, max: 0 }
    let sum = 0
    let max = 0
    const start = (ch.head - ch.count + RING_SIZE) % RING_SIZE
    for (let i = 0; i < ch.count; i++) {
      const v = ch.samples[(start + i) % RING_SIZE]!
      sum += v
      if (v > max) max = v
    }
    return { avg: sum / ch.count, max }
  }

  perfMarkStart = (channelName: string) => {
    const t0 = performance.now()
    const ch = getOrCreateChannel(channelName)
    return () => {
      pushSample(ch, performance.now() - t0)
    }
  }

  perfCountRender = (name: string) => {
    let rc = renderCounters.get(name)
    if (!rc) {
      rc = { count: 0, lastReset: performance.now(), rps: 0 }
      renderCounters.set(name, rc)
    }
    rc.count++
    const now = performance.now()
    const elapsed = now - rc.lastReset
    if (elapsed >= 1000) {
      rc.rps = Math.round((rc.count / elapsed) * 1000)
      rc.count = 0
      rc.lastReset = now
    }
  }

  // Frame budget monitor — rAF-based inter-frame delta tracking
  const FRAME_RING = 60
  const frameDeltas = new Float64Array(FRAME_RING)
  let frameHead = 0
  let frameCount = 0
  let framePrev = 0
  let frameDrops = 0
  let frameDropsLastReset = performance.now()
  let frameDropsPerSec = 0

  function frameTick(now: number) {
    if (framePrev > 0) {
      const delta = now - framePrev
      frameDeltas[frameHead] = delta
      frameHead = (frameHead + 1) % FRAME_RING
      if (frameCount < FRAME_RING) frameCount++
      if (delta > 20) frameDrops++
    }
    framePrev = now

    const elapsed = now - frameDropsLastReset
    if (elapsed >= 1000) {
      frameDropsPerSec = Math.round((frameDrops / elapsed) * 1000)
      frameDrops = 0
      frameDropsLastReset = now
    }

    requestAnimationFrame(frameTick)
  }
  requestAnimationFrame(frameTick)

  function frameBudgetStats(): { avgDelta: number; dropsPerSec: number } {
    if (frameCount === 0) return { avgDelta: 0, dropsPerSec: 0 }
    let sum = 0
    const start = (frameHead - frameCount + FRAME_RING) % FRAME_RING
    for (let i = 0; i < frameCount; i++) {
      sum += frameDeltas[(start + i) % FRAME_RING]!
    }
    return { avgDelta: sum / frameCount, dropsPerSec: frameDropsPerSec }
  }

  function buildSnapshot(): PerfSnapshot {
    const snap: PerfSnapshot = { channels: {}, renders: {}, heap: null, frameBudget: null }
    for (const [name, ch] of channels) {
      snap.channels[name] = channelStats(ch)
    }
    for (const [name, rc] of renderCounters) {
      snap.renders[name] = rc.rps
    }
    if (performance.memory) {
      snap.heap = {
        used: performance.memory.usedJSHeapSize,
        total: performance.memory.totalJSHeapSize,
      }
    }
    snap.frameBudget = frameBudgetStats()
    return snap
  }

  function startEmitting() {
    if (emitTimer) return
    emitTimer = setInterval(() => {
      if (listeners.size === 0) return
      const snap = buildSnapshot()
      for (const fn of listeners) fn(snap)
    }, 500) // 2fps
  }

  perfSubscribe = (listener: (snap: PerfSnapshot) => void) => {
    listeners.add(listener)
    startEmitting()
    return () => {
      listeners.delete(listener)
      if (listeners.size === 0 && emitTimer) {
        clearInterval(emitTimer)
        emitTimer = null
      }
    }
  }
}

export { perfMarkStart, perfCountRender, perfSubscribe }
