// @ts-check

/** @typedef {import('./types').Album} Album */
/** @typedef {import('./types').Track} Track */
/** @typedef {import('./types').Playlist} Playlist */
/** @typedef {import('./types').PlaybackState} PlaybackState */
/** @typedef {import('./types').LibraryData} LibraryData */
/** @typedef {import('./types').PersistedAlbum} PersistedAlbum */
/** @typedef {import('./types').PersistedTrack} PersistedTrack */
/** @typedef {import('./types').ArtUrlUpdates} ArtUrlUpdates */
/** @typedef {import('./types').StoreSchema} StoreSchema */

import { app } from 'electron'
import path from 'path'
import fs from 'fs'
import { createHash } from 'crypto'
import Store from 'electron-store'

const store = new Store({
  name: 'library',
  defaults: /** @type {StoreSchema} */ ({
    schemaVersion: 1,
    folders: [],
    albums: [],
    tracks: [],
    playlists: [],
  }),
})

const artworkDir = path.join(app.getPath('userData'), 'artwork')

/**
 * Ensure the artwork directory exists
 * @returns {void}
 */
function ensureArtworkDir() {
  fs.mkdirSync(artworkDir, { recursive: true })
}

/**
 * Check if an art string is a base64 data URI (not an SVG fallback or artwork:// URL)
 * @param {string | null | undefined} art
 * @returns {boolean}
 */
function isBase64DataUri(art) {
  return !!(art && art.startsWith('data:image/') && !art.startsWith('data:image/svg+xml'))
}

/**
 * Save base64 data URI as an image file named by content hash, so identical
 * covers (e.g. album art and a track's own art) share one file on disk.
 * Returns the filename (relative).
 * @param {string} dataUri
 * @returns {string | null}
 */
function saveArtwork(dataUri) {
  ensureArtworkDir()

  const match = dataUri.match(/^data:image\/(\w+);base64,(.+)$/)
  if (!match) return null

  const ext = match[1] === 'jpeg' ? 'jpg' : match[1]
  const buffer = Buffer.from(match[2], 'base64')
  const hash = createHash('sha256').update(buffer).digest('hex').slice(0, 16)
  const filename = `art-${hash}.${ext}`
  const filePath = path.join(artworkDir, filename)

  if (!fs.existsSync(filePath)) {
    fs.writeFileSync(filePath, buffer)
  }
  return filename
}

/**
 * Extract the artwork filename back out of an artwork:// URL
 * @param {string | null | undefined} art
 * @returns {string | null}
 */
function artFileFromUrl(art) {
  const prefix = 'artwork://file/'
  if (!art || !art.startsWith(prefix)) return null
  try {
    return decodeURIComponent(art.slice(prefix.length))
  } catch {
    return null
  }
}

/**
 * Get the absolute path for an artwork file.
 * Rejects filenames that would escape the artwork directory.
 * @param {string} filename
 * @returns {string | null}
 */
export function getArtworkPath(filename) {
  const resolved = path.resolve(artworkDir, filename)
  if (!resolved.startsWith(artworkDir + path.sep) && resolved !== artworkDir) {
    return null
  }
  return resolved
}

/**
 * Save library data to disk, externalizing base64 artwork to files
 * @param {LibraryData} data
 * @returns {ArtUrlUpdates}
 */
export function saveLibrary({ albums, tracks, folders }) {
  /** @type {ArtUrlUpdates} */
  const artUpdates = { albums: {}, tracks: {} }

  try {
    ensureArtworkDir()

    const persistedAlbums = albums.map((album) => {
      const { art, ...rest } = album

      // Base64 image data → externalize to file
      if (isBase64DataUri(art)) {
        const artFile = saveArtwork(/** @type {string} */ (art))
        if (artFile) {
          // Tell the renderer the new artwork:// URL so it can update in-memory
          artUpdates.albums[album.id] = `artwork://file/${encodeURIComponent(artFile)}`
          return { ...rest, artFile }
        }
      }

      // SVG fallback → keep inline (tiny)
      if (art && art.startsWith('data:image/svg+xml')) {
        return { ...rest, artSvg: art }
      }

      // artwork:// URL — already persisted, keep referencing the same file
      const existingFile = artFileFromUrl(art)
      if (existingFile) {
        return { ...rest, artFile: existingFile }
      }

      return rest
    })

    const persistedTracks = tracks.map((track) => {
      const { art, ...rest } = track

      if (isBase64DataUri(art)) {
        const artFile = saveArtwork(/** @type {string} */ (art))
        if (artFile) {
          artUpdates.tracks[track.id] = `artwork://file/${encodeURIComponent(artFile)}`
          return { ...rest, artFile }
        }
      }

      const existingFile = artFileFromUrl(art)
      if (existingFile) {
        return { ...rest, artFile: existingFile }
      }

      return rest
    })

    store.set('albums', /** @type {PersistedAlbum[]} */ (persistedAlbums))
    store.set('tracks', /** @type {PersistedTrack[]} */ (persistedTracks))
    store.set('folders', folders || [])
  } catch (err) {
    console.error('Failed to save library:', err)
  }

  return artUpdates
}

/**
 * Load library data from disk, converting artFile references to artwork:// URLs
 * @returns {LibraryData}
 */
export function loadLibrary() {
  try {
    const albums = store.get('albums', [])
    const tracks = store.get('tracks', [])
    const folders = store.get('folders', [])

    const hydratedAlbums = albums.map((album) => {
      const { artFile, artSvg, ...rest } = album

      if (artFile) {
        // Verify the file still exists
        const fullPath = path.join(artworkDir, artFile)
        if (fs.existsSync(fullPath)) {
          return { ...rest, art: `artwork://file/${encodeURIComponent(artFile)}` }
        }
      }

      if (artSvg) {
        return { ...rest, art: artSvg }
      }

      return { ...rest, art: null }
    })

    const hydratedTracks = tracks.map((track) => {
      const { artFile, ...rest } = track

      if (artFile && fs.existsSync(path.join(artworkDir, artFile))) {
        return { ...rest, art: `artwork://file/${encodeURIComponent(artFile)}` }
      }

      return { ...rest, art: null }
    })

    return { albums: /** @type {Album[]} */ (hydratedAlbums), tracks: /** @type {Track[]} */ (hydratedTracks), folders }
  } catch (err) {
    console.error('Failed to load library:', err)
    return { albums: [], tracks: [], folders: [] }
  }
}

/**
 * Save playback state (current track index and position)
 * @param {PlaybackState} data
 * @returns {void}
 */
export function savePlaybackState({ currentTrackIndex, currentTime, playQueue, queueIndex, shuffle }) {
  store.set('playbackState', { currentTrackIndex, currentTime, playQueue, queueIndex, shuffle })
}

/**
 * Load playback state
 * @returns {PlaybackState}
 */
export function loadPlaybackState() {
  return store.get('playbackState', { currentTrackIndex: 0, currentTime: 0, playQueue: [], queueIndex: -1, shuffle: false })
}

/**
 * Save playlists to disk
 * @param {Playlist[]} playlists
 * @returns {void}
 */
export function savePlaylists(playlists) {
  store.set('playlists', playlists)
}

/**
 * Load playlists from disk
 * @returns {Playlist[]}
 */
export function loadPlaylists() {
  return store.get('playlists', [])
}

/**
 * Clear all persisted library data and artwork files
 * @returns {void}
 */
export function clearLibrary() {
  try {
    store.clear()
    store.set('schemaVersion', 1)

    // Remove all artwork files — per-file try/catch so one locked file
    // doesn't leave the rest orphaned on disk.
    if (fs.existsSync(artworkDir)) {
      const files = fs.readdirSync(artworkDir)
      for (const file of files) {
        try {
          fs.unlinkSync(path.join(artworkDir, file))
        } catch (err) {
          console.error(`Failed to delete artwork file ${file}:`, err)
        }
      }
    }
  } catch (err) {
    console.error('Failed to clear library:', err)
  }
}
