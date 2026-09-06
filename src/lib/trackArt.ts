import type { Album, Track } from '../types'

/**
 * Resolve the artwork to display for a track: the track's own art (set only
 * when it differs from the album cover) wins, then the album art.
 */
export function resolveTrackArt(
  track: Track | null | undefined,
  album: Album | null | undefined,
): string | null {
  return track?.art ?? album?.art ?? null
}
