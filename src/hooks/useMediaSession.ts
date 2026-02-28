import { useEffect, useRef } from 'react'
import { playbackTimeStore } from '../lib/playbackTimeStore'
import type { Track, Album } from '../types'

interface MediaSessionOptions {
  track: Track | null | undefined
  album: (Album & { dominantColor: string; accentColor: string }) | null
  isPlaying: boolean
  duration: number
  onPlay: () => void
  onPause: () => void
  onNext: () => void
  onPrev: () => void
  onSeek: (seconds: number) => void
}

/**
 * Integrates with the Media Session API to provide:
 * - OS media controls (taskbar, lock screen, notification area)
 * - Hardware media key support (play/pause/next/prev)
 * - System-level track metadata and artwork display
 */
export default function useMediaSession({
  track,
  album,
  isPlaying,
  duration,
  onPlay,
  onPause,
  onNext,
  onPrev,
  onSeek,
}: MediaSessionOptions) {
  // Keep callbacks in refs to avoid re-registering handlers on every render
  const onPlayRef = useRef(onPlay)
  const onPauseRef = useRef(onPause)
  const onNextRef = useRef(onNext)
  const onPrevRef = useRef(onPrev)
  const onSeekRef = useRef(onSeek)
  const artworkBlobUrlRef = useRef<string | null>(null)

  useEffect(() => { onPlayRef.current = onPlay }, [onPlay])
  useEffect(() => { onPauseRef.current = onPause }, [onPause])
  useEffect(() => { onNextRef.current = onNext }, [onNext])
  useEffect(() => { onPrevRef.current = onPrev }, [onPrev])
  useEffect(() => { onSeekRef.current = onSeek }, [onSeek])

  // Register action handlers once
  useEffect(() => {
    if (!('mediaSession' in navigator)) return

    const handlers: [MediaSessionAction, MediaSessionActionHandler][] = [
      ['play', () => onPlayRef.current()],
      ['pause', () => onPauseRef.current()],
      ['nexttrack', () => onNextRef.current()],
      ['previoustrack', () => onPrevRef.current()],
      ['seekto', (details) => {
        if (details.seekTime != null) {
          onSeekRef.current(details.seekTime)
        }
      }],
      ['seekbackward', (details) => {
        const skipTime = details.seekOffset ?? 10
        const current = playbackTimeStore.get()
        onSeekRef.current(Math.max(0, current - skipTime))
      }],
      ['seekforward', (details) => {
        const skipTime = details.seekOffset ?? 10
        const current = playbackTimeStore.get()
        onSeekRef.current(current + skipTime)
      }],
    ]

    for (const [action, handler] of handlers) {
      try {
        navigator.mediaSession.setActionHandler(action, handler)
      } catch {
        // Some actions may not be supported in all environments
      }
    }

    return () => {
      for (const [action] of handlers) {
        try {
          navigator.mediaSession.setActionHandler(action, null)
        } catch {}
      }
    }
  }, [])

  // Update metadata when track or album changes.
  // Artwork uses custom artwork:// protocol which the OS can't resolve directly,
  // so we fetch it and create a blob URL that Chromium can pass to Windows SMTC.
  useEffect(() => {
    if (!('mediaSession' in navigator) || !track) return

    let cancelled = false

    const currentTrack = track

    async function updateMetadata() {
      // Revoke previous blob URL
      if (artworkBlobUrlRef.current) {
        URL.revokeObjectURL(artworkBlobUrlRef.current)
        artworkBlobUrlRef.current = null
      }

      const artwork: MediaImage[] = []

      if (album?.art && !album.art.startsWith('data:image/svg')) {
        try {
          const response = await fetch(album.art)
          const blob = await response.blob()
          if (cancelled) return
          const blobUrl = URL.createObjectURL(blob)
          artworkBlobUrlRef.current = blobUrl
          artwork.push({
            src: blobUrl,
            sizes: '512x512',
            type: blob.type || 'image/jpeg',
          })
        } catch {
          // Artwork fetch failed — proceed without it
        }
      }

      if (cancelled) return

      navigator.mediaSession.metadata = new MediaMetadata({
        title: currentTrack.title,
        artist: currentTrack.artist,
        album: album?.title ?? '',
        artwork,
      })
    }

    updateMetadata()

    return () => {
      cancelled = true
    }
  }, [track?.id, album?.id, album?.art])

  // Clean up blob URL on unmount
  useEffect(() => {
    return () => {
      if (artworkBlobUrlRef.current) {
        URL.revokeObjectURL(artworkBlobUrlRef.current)
      }
    }
  }, [])

  // Update playback state
  useEffect(() => {
    if (!('mediaSession' in navigator)) return
    navigator.mediaSession.playbackState = isPlaying ? 'playing' : 'paused'
  }, [isPlaying])

  // Update position state so the system scrubber stays in sync
  useEffect(() => {
    if (!('mediaSession' in navigator) || !track || !duration) return

    const updatePosition = () => {
      try {
        navigator.mediaSession.setPositionState({
          duration,
          playbackRate: 1,
          position: Math.min(playbackTimeStore.get(), duration),
        })
      } catch {
        // position may be invalid during track transitions
      }
    }

    // Set initial position
    updatePosition()

    if (!isPlaying) return

    // Update periodically while playing
    const interval = setInterval(updatePosition, 1000)
    return () => clearInterval(interval)
  }, [isPlaying, duration, track?.id])
}
