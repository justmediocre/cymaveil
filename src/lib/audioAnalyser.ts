/**
 * AudioContext + AnalyserNode singleton.
 * Guards createMediaElementSource() one-call-per-element limit.
 */

let audioContext: AudioContext | null = null
let analyserNode: AnalyserNode | null = null
let sourceNode: MediaElementAudioSourceNode | null = null
let gainNode: GainNode | null = null
let connectedElement: HTMLAudioElement | null = null
let analyserConnected = false

/**
 * Get or create an AnalyserNode connected to the given audio element.
 * Must be called after a user gesture (AudioContext requires it).
 */
export function getOrCreateAnalyser(audioElement: HTMLAudioElement | null): AnalyserNode | null {
  if (!audioElement) return null

  // Already connected to this element
  if (connectedElement === audioElement && analyserNode) {
    // Resume context if suspended (e.g. after tab unfocus)
    if (audioContext!.state === 'suspended') {
      audioContext!.resume()
    }
    return analyserNode
  }

  try {
    if (!audioContext) {
      audioContext = new AudioContext()
    }

    if (audioContext.state === 'suspended') {
      audioContext.resume()
    }

    // Only create source once per element (browser enforces this)
    if (connectedElement !== audioElement) {
      sourceNode = audioContext.createMediaElementSource(audioElement)
      connectedElement = audioElement
    }

    analyserNode = audioContext.createAnalyser()
    analyserNode.fftSize = 512
    analyserNode.smoothingTimeConstant = 0.4

    if (!gainNode) {
      gainNode = audioContext.createGain()
    }

    // Connect: source -> analyser (full amplitude for visualizer)
    //          source -> gainNode -> destination (volume-controlled playback)
    sourceNode!.disconnect()
    sourceNode!.connect(analyserNode)
    sourceNode!.connect(gainNode)
    gainNode.connect(audioContext.destination)
    analyserConnected = true

    return analyserNode
  } catch (e) {
    if (import.meta.env.DEV) console.warn('Failed to create audio analyser:', e)
    return null
  }
}

/**
 * Connect or disconnect the AnalyserNode from the audio graph.
 * When disconnected, the AnalyserNode stops performing FFT, saving CPU.
 * Playback continues via gainNode -> destination.
 */
export function setAnalyserEnabled(enabled: boolean) {
  if (!sourceNode || !analyserNode) return
  if (enabled && !analyserConnected) {
    sourceNode.connect(analyserNode)
    analyserConnected = true
  } else if (!enabled && analyserConnected) {
    try { sourceNode.disconnect(analyserNode) } catch {}
    analyserConnected = false
  }
}

/**
 * Set the playback volume via the Web Audio GainNode.
 * This controls speaker output without affecting the AnalyserNode,
 * so the visualizer always sees full-amplitude data.
 */
export function setAnalyserVolume(vol: number) {
  if (gainNode) {
    gainNode.gain.value = Math.max(0, Math.min(1, vol))
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
