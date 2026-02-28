import React, { useRef, useEffect, useCallback } from 'react'
import { getOrCreateAnalyser, setAnalyserEnabled } from '../lib/audioAnalyser'
import { visualSettingsStore } from '../lib/visualSettingsStore'

/**
 * React hook that manages the Web Audio API analyser lifecycle.
 * Initializes on first play (user gesture required for AudioContext).
 * Disconnects the AnalyserNode when no visual effects need it (saves FFT CPU).
 * Returns analyserRef for direct canvas reads — no React state per frame.
 */
export default function useAudioAnalyser(audioRef: React.RefObject<HTMLAudioElement | null>, isPlaying: boolean) {
  const analyserRef = useRef<AnalyserNode | null>(null)
  const dataArrayRef = useRef<Uint8Array | null>(null)

  const initAnalyser = useCallback(() => {
    if (analyserRef.current) return
    const audio = audioRef?.current
    if (!audio) return

    const analyser = getOrCreateAnalyser(audio)
    if (analyser) {
      analyserRef.current = analyser
      dataArrayRef.current = new Uint8Array(analyser.frequencyBinCount)
    }
  }, [audioRef])

  // Initialize analyser when playback starts, but only if visuals need it
  useEffect(() => {
    if (isPlaying) {
      const s = visualSettingsStore.get()
      if (s.canvasVisualizer || s.bassShake) {
        initAnalyser()
      }
    }
  }, [isPlaying, initAnalyser])

  // Connect/disconnect analyser based on visual settings
  useEffect(() => {
    const sync = () => {
      const s = visualSettingsStore.get()
      const needs = s.canvasVisualizer || s.bassShake
      if (needs && !analyserRef.current && isPlaying) {
        initAnalyser()
      }
      setAnalyserEnabled(needs)
    }
    sync()
    return visualSettingsStore.subscribe(sync)
  }, [isPlaying, initAnalyser])

  return { analyserRef, dataArrayRef, initAnalyser }
}
