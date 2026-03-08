// @ts-check

/** @typedef {import('./types').Album} Album */
/** @typedef {import('./types').Track} Track */
/** @typedef {import('./types').ScanResult} ScanResult */
/** @typedef {import('./types').SingleFileScanResult} SingleFileScanResult */
/** @typedef {import('./types').ParsedAudioFile} ParsedAudioFile */
/** @typedef {import('./types').AlbumBuildEntry} AlbumBuildEntry */

import { readdir, stat } from 'fs/promises'
import { createHash } from 'crypto'
import path from 'path'
import { parseFile } from 'music-metadata'

export const AUDIO_EXTENSIONS = new Set(['.mp3', '.flac', '.m4a', '.ogg', '.wav', '.aiff', '.aac', '.wma'])
const BATCH_SIZE = 20

/**
 * Recursively find all audio files in a directory
 * @param {string} dir
 * @returns {Promise<string[]>}
 */
export async function findAudioFiles(dir) {
  /** @type {string[]} */
  const files = []

  /** @param {string} currentDir */
  async function walk(currentDir) {
    let entries
    try {
      entries = await readdir(currentDir, { withFileTypes: true })
    } catch {
      return // skip unreadable dirs
    }

    for (const entry of entries) {
      const fullPath = path.join(currentDir, entry.name)
      if (entry.isDirectory()) {
        await walk(fullPath)
      } else if (AUDIO_EXTENSIONS.has(path.extname(entry.name).toLowerCase())) {
        files.push(fullPath)
      }
    }
  }

  await walk(dir)
  return files
}

/**
 * Convert embedded picture buffer to a base64 data URI
 * @param {{ data?: Uint8Array, format?: string } | null | undefined} picture
 * @returns {string | null}
 */
function pictureToDataUri(picture) {
  if (!picture || !picture.data || !picture.data.length) return null
  const mime = picture.format || 'image/jpeg'
  const base64 = Buffer.from(picture.data).toString('base64')
  return `data:${mime};base64,${base64}`
}

/**
 * Generate a deterministic hue from a string (for fallback colors)
 * @param {string} str
 * @returns {number}
 */
function hashHue(str) {
  let hash = 0
  for (let i = 0; i < str.length; i++) {
    hash = str.charCodeAt(i) + ((hash << 5) - hash)
  }
  return Math.abs(hash) % 360
}

/**
 * Generate fallback SVG gradient art for albums missing artwork
 * @param {string} albumTitle
 * @param {string} artist
 * @returns {string}
 */
function generateFallbackArt(albumTitle, artist) {
  const hue = hashHue(`${artist}-${albumTitle}`)
  const c1 = `hsl(${hue}, 40%, 15%)`
  const c2 = `hsl(${hue}, 50%, 25%)`
  const c3 = `hsl(${hue}, 60%, 40%)`
  return `data:image/svg+xml,${encodeURIComponent(`
<svg xmlns="http://www.w3.org/2000/svg" width="600" height="600">
  <defs>
    <radialGradient id="a" cx="30%" cy="30%" r="70%">
      <stop offset="0%" stop-color="${c1}"/>
      <stop offset="50%" stop-color="${c2}"/>
      <stop offset="100%" stop-color="${c3}"/>
    </radialGradient>
    <radialGradient id="b" cx="70%" cy="70%" r="60%">
      <stop offset="0%" stop-color="${c2}" stop-opacity="0.6"/>
      <stop offset="100%" stop-color="${c3}" stop-opacity="0"/>
    </radialGradient>
  </defs>
  <rect width="600" height="600" fill="url(#a)"/>
  <rect width="600" height="600" fill="url(#b)"/>
  <circle cx="200" cy="350" r="180" fill="${c1}" opacity="0.15"/>
  <circle cx="420" cy="200" r="140" fill="${c3}" opacity="0.2"/>
</svg>`)}`
}

/**
 * Generate placeholder dominant/accent colors from a hash
 * @param {string} key
 * @returns {{ dominantColor: string, accentColor: string }}
 */
function generateColors(key) {
  const hue = hashHue(key)
  return {
    dominantColor: `hsl(${hue}, 40%, 20%)`,
    accentColor: `hsl(${hue}, 60%, 55%)`,
  }
}

/**
 * Parse metadata from a single audio file
 * @param {string} filePath
 * @returns {Promise<ParsedAudioFile>}
 */
async function parseAudioFile(filePath) {
  try {
    const metadata = await parseFile(filePath)
    const { common, format } = metadata

    const title = common.title || path.basename(filePath, path.extname(filePath))
    const artist = common.artist || 'Unknown Artist'
    const album = common.album || 'Unknown Album'
    const year = common.year || null
    const trackNum = common.track?.no || null
    const duration = Math.round(format.duration || 0)

    // Extract embedded picture
    let artDataUri = null
    if (common.picture && common.picture.length > 0) {
      artDataUri = pictureToDataUri(common.picture[0])
    }

    return {
      filePath,
      title,
      artist,
      album,
      year,
      trackNum,
      duration,
      artDataUri,
    }
  } catch {
    // Gracefully handle files that can't be parsed
    return {
      filePath,
      title: path.basename(filePath, path.extname(filePath)),
      artist: 'Unknown Artist',
      album: 'Unknown Album',
      year: null,
      trackNum: null,
      duration: 0,
      artDataUri: null,
    }
  }
}

/**
 * Process files in batches to avoid memory spikes
 * @param {string[]} files
 * @param {((count: number) => void) | undefined} onProgress
 * @returns {Promise<ParsedAudioFile[]>}
 */
async function parseBatch(files, onProgress) {
  /** @type {ParsedAudioFile[]} */
  const results = []
  for (let i = 0; i < files.length; i += BATCH_SIZE) {
    const batch = files.slice(i, i + BATCH_SIZE)
    const batchResults = await Promise.all(batch.map(parseAudioFile))
    results.push(...batchResults)
    onProgress?.(Math.min(i + BATCH_SIZE, files.length))
  }
  return results
}

/**
 * Scan a folder for music files and return { albums, tracks } matching the app's data shapes
 * @param {string} folderPath
 * @param {((progress: { phase: string, current: number, total: number }) => void) | undefined} [onProgress]
 * @returns {Promise<ScanResult>}
 */
export async function scanMusicFolder(folderPath, onProgress) {
  const audioFiles = await findAudioFiles(folderPath)

  if (audioFiles.length === 0) {
    return { albums: [], tracks: [] }
  }

  const total = audioFiles.length
  onProgress?.({ phase: 'reading', current: 0, total })

  const parsed = await parseBatch(audioFiles, (current) => {
    onProgress?.({ phase: 'reading', current, total })
  })

  // Group tracks into albums by album name only (handles various-artist compilations)
  /** @type {Map<string, AlbumBuildEntry>} */
  const albumMap = new Map()

  for (const file of parsed) {
    const albumKey = file.album

    if (!albumMap.has(albumKey)) {
      const albumId = `imported-${albumMap.size + 1}`
      const colors = generateColors(albumKey)
      albumMap.set(albumKey, {
        id: albumId,
        title: file.album,
        artists: new Set([file.artist]),
        year: file.year,
        art: file.artDataUri || generateFallbackArt(file.album, file.artist),
        dominantColor: colors.dominantColor,
        accentColor: colors.accentColor,
        hasRealArt: !!file.artDataUri,
        tracks: [],
      })
    } else {
      const existing = /** @type {AlbumBuildEntry} */ (albumMap.get(albumKey))
      existing.artists.add(file.artist)
    }

    const albumEntry = /** @type {AlbumBuildEntry} */ (albumMap.get(albumKey))

    // Update album art if this track has art and the album doesn't yet
    if (file.artDataUri && !albumEntry.hasRealArt) {
      albumEntry.art = file.artDataUri
      albumEntry.hasRealArt = true
    }

    // Update year if this track has one and album doesn't
    if (file.year && !albumEntry.year) {
      albumEntry.year = file.year
    }

    albumEntry.tracks.push({
      title: file.title,
      artist: file.artist,
      duration: file.duration,
      trackNum: file.trackNum,
      filePath: file.filePath,
    })
  }

  // Build final albums and tracks arrays
  /** @type {Album[]} */
  const albums = []
  /** @type {Track[]} */
  const tracks = []
  let trackCounter = 0

  for (const [, albumEntry] of albumMap) {
    // Sort tracks by track number, then by title
    albumEntry.tracks.sort((a, b) => {
      if (a.trackNum && b.trackNum) return a.trackNum - b.trackNum
      if (a.trackNum) return -1
      if (b.trackNum) return 1
      return a.title.localeCompare(b.title)
    })

    const artist = albumEntry.artists.size === 1
      ? [...albumEntry.artists][0]
      : 'Various Artists'

    const album = {
      id: albumEntry.id,
      title: albumEntry.title,
      artist,
      year: albumEntry.year,
      art: albumEntry.art,
      dominantColor: albumEntry.dominantColor,
      accentColor: albumEntry.accentColor,
    }
    albums.push(album)

    for (let i = 0; i < albumEntry.tracks.length; i++) {
      const t = albumEntry.tracks[i]
      trackCounter++
      tracks.push({
        id: `imported-t-${trackCounter}`,
        title: t.title,
        artist: t.artist,
        albumId: albumEntry.id,
        duration: t.duration,
        trackNum: t.trackNum || i + 1,
        filePath: t.filePath,
      })
    }
  }

  // Sort albums by artist then title
  albums.sort((a, b) => a.artist.localeCompare(b.artist) || a.title.localeCompare(b.title))

  return { albums, tracks }
}

/**
 * Deterministic short hash for generating stable IDs
 * @param {string} input
 * @returns {string}
 */
function stableId(input) {
  return createHash('sha256').update(input).digest('hex').slice(0, 12)
}

/**
 * Scan a single audio file and return { album, track } with deterministic IDs.
 * The same file will always produce the same IDs.
 * @param {string} filePath
 * @returns {Promise<SingleFileScanResult>}
 */
export async function scanSingleFile(filePath) {
  const parsed = await parseAudioFile(filePath)

  const albumKey = parsed.album
  const albumId = `album-${stableId(albumKey)}`
  const trackId = `track-${stableId(filePath)}`

  const colors = generateColors(albumKey)
  const art = parsed.artDataUri || generateFallbackArt(parsed.album, parsed.artist)

  const album = {
    id: albumId,
    title: parsed.album,
    artist: parsed.artist,
    year: parsed.year,
    art,
    dominantColor: colors.dominantColor,
    accentColor: colors.accentColor,
  }

  const track = {
    id: trackId,
    title: parsed.title,
    artist: parsed.artist,
    albumId,
    duration: parsed.duration,
    trackNum: parsed.trackNum || 0,
    filePath,
  }

  return { album, track }
}
