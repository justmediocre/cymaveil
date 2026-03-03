import { useEffect, useRef } from 'react'
import { usePlayback } from '../contexts/playback/PlaybackContext'
import { useTheme } from '../contexts/ThemeContext'
import { visualSettingsStore } from '../lib/visualSettingsStore'
import type { VisualSettings } from '../types'

interface ScreenshotModeProps {
  setActiveNav: (nav: string) => void
  setSidebarOpen: (open: boolean) => void
  setShowQueue: (show: boolean) => void
}

/** Wait for a condition to be true, polling at `interval` ms, up to `timeout` ms */
function waitFor(check: () => boolean, timeout: number, interval = 200): Promise<boolean> {
  return new Promise((resolve) => {
    if (check()) { resolve(true); return }
    const start = Date.now()
    const id = setInterval(() => {
      if (check()) { clearInterval(id); resolve(true) }
      else if (Date.now() - start > timeout) { clearInterval(id); resolve(false) }
    }, interval)
  })
}

/** Wait for one requestAnimationFrame */
function raf(): Promise<void> {
  return new Promise((resolve) => requestAnimationFrame(() => resolve()))
}

/** Wait for a fixed duration */
function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

/**
 * Dev-only hook that orchestrates automated screenshot capture.
 * Starts automatically on mount when `isScreenshotMode` is true.
 *
 * Flow:
 * 1. Apply visual settings overrides
 * 2. Navigate to NowPlaying, collapse sidebar/queue
 * 3. Wait for library + playback restore, start playing
 * 4. Wait for depth segmentation to complete (data-foreground-mask)
 * 5. Capture dark screenshot
 * 6. Toggle to light theme, capture light screenshot
 * 7. Restore settings, run combine script, exit
 */
export default function useScreenshotMode({ setActiveNav, setSidebarOpen, setShowQueue }: ScreenshotModeProps) {
  const { handlePlayPause, seek, currentTrack } = usePlayback()
  const { toggleTheme } = useTheme()
  const runRef = useRef(false)

  // Keep refs to latest values so the async flow always reads current state
  const currentTrackRef = useRef(currentTrack)
  currentTrackRef.current = currentTrack
  const handlePlayPauseRef = useRef(handlePlayPause)
  handlePlayPauseRef.current = handlePlayPause
  const seekRef = useRef(seek)
  seekRef.current = seek
  const toggleThemeRef = useRef(toggleTheme)
  toggleThemeRef.current = toggleTheme

  useEffect(() => {
    if (!window.electronAPI?.isScreenshotMode) return

    // Start directly on mount — no IPC message needed
    if (runRef.current) return
    runRef.current = true

    ;(async () => {
      console.log('[screenshot] Starting automated capture flow')

      // 1. Save current visual settings and apply overrides
      const savedSettings: VisualSettings = { ...visualSettingsStore.get() }
      visualSettingsStore.setBulk({
        visualizerStyle: 'full-surface',
        bassShake: false,
        mosaicDensity: 4,
        backgroundMosaic: true,
        canvasVisualizer: true,
        depthLayerEnabled: true,
      })

      // 2. Navigate to NowPlaying, hide sidebar and queue
      setActiveNav('NowPlaying')
      setSidebarOpen(false)
      setShowQueue(false)

      // 3. Wait for currentTrack to be defined (library + playback restoration)
      console.log('[screenshot] Waiting for track to load...')
      await waitFor(() => !!currentTrackRef.current, 15000)

      // Small delay for React to settle after restoration
      await sleep(500)

      // 4. Start playback and seek to 0:50
      handlePlayPauseRef.current()
      await sleep(300)
      seekRef.current(50)

      // 5. Adaptive wait for depth segmentation + UI settle
      console.log('[screenshot] Waiting for visualizer + depth segmentation...')
      // Minimum wait for visualizer bars, album colors, mosaic tiles
      await sleep(2500)

      // Poll for [data-foreground-mask] in the DOM (segmentation complete)
      const maskFound = await waitFor(
        () => !!document.querySelector('[data-foreground-mask]'),
        30000,
      )
      if (!maskFound) {
        console.warn('[screenshot] Depth mask not found after 30s, proceeding anyway')
      } else {
        console.log('[screenshot] Depth mask detected')
      }

      // 6. Seek to 0:50 and wait for the compositor to
      //    finish painting the seeked frame before capturing
      seekRef.current(50)
      await sleep(200)

      // 7. Capture dark screenshot
      console.log('[screenshot] Capturing dark theme...')
      await window.electronAPI!.screenshotCapture!('dark')

      // 8. Toggle to light theme
      toggleThemeRef.current()
      await sleep(800) // Wait for CSS variable transition

      // 9. Re-seek and settle for light capture
      seekRef.current(50)
      await sleep(200)

      // 10. Capture light screenshot
      console.log('[screenshot] Capturing light theme...')
      await window.electronAPI!.screenshotCapture!('light')

      // 11. Restore original visual settings
      visualSettingsStore.setBulk(savedSettings)

      // 12. Run combine script and exit
      console.log('[screenshot] Running combine script...')
      await window.electronAPI!.screenshotCombine!()
    })()
  }, [setActiveNav, setSidebarOpen, setShowQueue])
}
