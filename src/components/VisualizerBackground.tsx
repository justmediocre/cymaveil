import { useRef, useEffect, memo } from 'react'
import type React from 'react'
import { perfMarkStart } from '../lib/perf'
import { onTick } from '../lib/tickLoop'

interface VisualizerBackgroundProps {
  analyserRef: React.RefObject<AnalyserNode | null>
  dominantColor: string
  accentColor: string
  isPlaying: boolean
  bassEnergyRef: React.MutableRefObject<number>
}

/**
 * Bass energy computation.
 * Reads bass energy at ~30fps (throttled) and writes to bassEnergyRef
 * (shared with AlbumArt for bass-hit zoom) instead of React state,
 * so the App tree never re-renders from bass updates.
 * Uses its own data array to avoid conflicts with the Visualizer canvas.
 */
export default memo(function VisualizerBackground({ analyserRef, isPlaying, bassEnergyRef }: VisualizerBackgroundProps) {
  const localDataRef = useRef<Uint8Array<ArrayBuffer> | null>(null)

  useEffect(() => {
    if (!isPlaying) {
      bassEnergyRef.current = 0
      return
    }

    return onTick(() => {
      const markEnd = perfMarkStart('vizBg:tick')
      const analyser = analyserRef?.current
      if (analyser) {
        if (!localDataRef.current || localDataRef.current.length !== analyser.frequencyBinCount) {
          localDataRef.current = new Uint8Array(analyser.frequencyBinCount)
        }

        analyser.getByteFrequencyData(localDataRef.current)

        let sum = 0
        const bassBins = Math.min(3, localDataRef.current.length)  // ~0-258Hz at 512 fftSize
        for (let i = 0; i < bassBins; i++) {
          sum += localDataRef.current[i]!
        }
        const energy = sum / (bassBins * 255)
        bassEnergyRef.current = energy
      }
      markEnd()
    })
  }, [isPlaying, analyserRef, bassEnergyRef])

  return null
})
