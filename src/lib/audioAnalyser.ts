/**
 * AudioContext + AnalyserNode singleton with dual-deck support for crossfade.
 *
 * Audio graph:
 *   deckA.element → deckA.sourceNode → deckA.fadeGain ─┐
 *                                                       ├→ analyserNode (mixed signal for visualizer)
 *   deckB.element → deckB.sourceNode → deckB.fadeGain ─┘
 *                                                       ├→ masterGainNode → destination (volume control)
 */

export type DeckId = 'A' | 'B'

interface Deck {
  element: HTMLAudioElement
  sourceNode: MediaElementAudioSourceNode
  fadeGain: GainNode
}

let audioContext: AudioContext | null = null
let analyserNode: AnalyserNode | null = null
let masterGainNode: GainNode | null = null
let analyserConnected = false

const decks: Record<DeckId, Deck | null> = { A: null, B: null }

/** Shared merge point that both deck fadeGains connect to */
let mergeNode: GainNode | null = null

function ensureContext(): AudioContext {
  if (!audioContext) {
    audioContext = new AudioContext()
  }
  if (audioContext.state === 'suspended') {
    audioContext.resume()
  }
  return audioContext
}

function ensureMergeGraph(ctx: AudioContext) {
  if (mergeNode) return

  mergeNode = ctx.createGain()

  analyserNode = ctx.createAnalyser()
  analyserNode.fftSize = 512
  analyserNode.smoothingTimeConstant = 0.4

  masterGainNode = ctx.createGain()

  // merge → analyser (full amplitude for visualizer)
  // merge → masterGain → destination (volume-controlled playback)
  mergeNode.connect(analyserNode)
  mergeNode.connect(masterGainNode)
  masterGainNode.connect(ctx.destination)
  analyserConnected = true
}

function initDeck(deckId: DeckId, element: HTMLAudioElement): Deck | null {
  // Already connected to this element
  if (decks[deckId]?.element === element) return decks[deckId]

  try {
    const ctx = ensureContext()
    ensureMergeGraph(ctx)

    const sourceNode = ctx.createMediaElementSource(element)
    const fadeGain = ctx.createGain()
    // Deck A starts at 1 (active), deck B starts at 0 (standby)
    fadeGain.gain.value = deckId === 'A' ? 1 : 0

    sourceNode.connect(fadeGain)
    fadeGain.connect(mergeNode!)

    const deck: Deck = { element, sourceNode, fadeGain }
    decks[deckId] = deck
    return deck
  } catch (e) {
    if (import.meta.env.DEV) console.warn(`Failed to init deck ${deckId}:`, e)
    return null
  }
}

/**
 * Get or create an AnalyserNode connected to deck A's audio element.
 * Must be called after a user gesture (AudioContext requires it).
 */
export function getOrCreateAnalyser(audioElement: HTMLAudioElement | null): AnalyserNode | null {
  if (!audioElement) return null

  // Already connected to this element as deck A
  if (decks.A?.element === audioElement && analyserNode) {
    if (audioContext!.state === 'suspended') {
      audioContext!.resume()
    }
    return analyserNode
  }

  const deck = initDeck('A', audioElement)
  return deck ? analyserNode : null
}

/**
 * Initialize deck B into the same audio graph.
 * Call after deck A is initialized and the secondary audio element is ready.
 */
export function initSecondDeck(audioElement: HTMLAudioElement | null): boolean {
  if (!audioElement) return false
  if (decks.B?.element === audioElement) return true
  return initDeck('B', audioElement) !== null
}

/**
 * Set the fade gain for a specific deck.
 * If rampDuration > 0, uses linearRampToValueAtTime for smooth crossfade.
 */
export function setDeckFadeGain(deckId: DeckId, value: number, rampDuration = 0) {
  const deck = decks[deckId]
  if (!deck || !audioContext) return

  const clamped = Math.max(0, Math.min(1, value))
  const param = deck.fadeGain.gain

  if (rampDuration > 0) {
    // Cancel any in-progress ramp, set current value, then ramp to target
    param.cancelScheduledValues(audioContext.currentTime)
    param.setValueAtTime(param.value, audioContext.currentTime)
    param.linearRampToValueAtTime(clamped, audioContext.currentTime + rampDuration)
  } else {
    param.cancelScheduledValues(audioContext.currentTime)
    param.setValueAtTime(clamped, audioContext.currentTime)
  }
}

/**
 * Cancel any in-progress fade ramps on both decks and snap to current values.
 */
export function cancelFadeRamps() {
  if (!audioContext) return
  for (const deckId of ['A', 'B'] as DeckId[]) {
    const deck = decks[deckId]
    if (deck) {
      deck.fadeGain.gain.cancelScheduledValues(audioContext.currentTime)
    }
  }
}

/**
 * Connect or disconnect the AnalyserNode from the merge graph.
 * When disconnected, the AnalyserNode stops performing FFT, saving CPU.
 * Playback continues via mergeNode -> masterGainNode -> destination.
 */
export function setAnalyserEnabled(enabled: boolean) {
  if (!mergeNode || !analyserNode) return
  if (enabled && !analyserConnected) {
    mergeNode.connect(analyserNode)
    analyserConnected = true
  } else if (!enabled && analyserConnected) {
    try { mergeNode.disconnect(analyserNode) } catch {}
    analyserConnected = false
  }
}

/**
 * Set the playback volume via the master GainNode.
 * This controls speaker output without affecting the AnalyserNode,
 * so the visualizer always sees full-amplitude data.
 */
export function setAnalyserVolume(vol: number) {
  if (masterGainNode) {
    masterGainNode.gain.value = Math.max(0, Math.min(1, vol))
  }
}

/**
 * Read current frequency data into the provided buffer.
 */
export function getFrequencyData(analyser: AnalyserNode | null, dataArray: Uint8Array<ArrayBuffer>) {
  if (analyser) {
    analyser.getByteFrequencyData(dataArray)
  }
}

/**
 * Read current time-domain (waveform) data into the provided buffer.
 */
export function getTimeDomainData(analyser: AnalyserNode | null, dataArray: Uint8Array<ArrayBuffer>) {
  if (analyser) {
    analyser.getByteTimeDomainData(dataArray)
  }
}
