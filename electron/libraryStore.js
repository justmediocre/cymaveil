// @ts-check

/** @typedef {import('./types').Album} Album */
/** @typedef {import('./types').Track} Track */
/** @typedef {import('./types').Playlist} Playlist */
/** @typedef {import('./types').PlaybackState} PlaybackState */
/** @typedef {import('./types').LibraryData} LibraryData */
/** @typedef {import('./types').PersistedAlbum} PersistedAlbum */
/** @typedef {import('./types').StoreSchema} StoreSchema */

import { app } from 'electron'
import path from 'path'
import fs from 'fs'
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
 * Save base64 data URI as an image file. Returns the filename (relative).
 * @param {string} albumId
 * @param {string} dataUri
 * @returns {string | null}
 */
function saveArtwork(albumId, dataUri) {
  ensureArtworkDir()

  const match = dataUri.match(/^data:image\/(\w+);base64,(.+)$/)
  if (!match) return null

  const ext = match[1] === 'jpeg' ? 'jpg' : match[1]
  const buffer = Buffer.from(match[2], 'base64')
  const filename = `${albumId}.${ext}`
  const filePath = path.join(artworkDir, filename)

  fs.writeFileSync(filePath, buffer)
  return filename
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
 * @returns {void}
 */
export function saveLibrary({ albums, tracks, folders }) {
  try {
    ensureArtworkDir()

    const persistedAlbums = albums.map((album) => {
      const { art, ...rest } = album

      // Base64 image data → externalize to file
      if (isBase64DataUri(art)) {
        const artFile = saveArtwork(album.id, /** @type {string} */ (art))
        if (artFile) {
          return { ...rest, artFile }
        }
      }

      // SVG fallback → keep inline (tiny)
      if (art && art.startsWith('data:image/svg+xml')) {
        return { ...rest, artSvg: art }
      }

      // artwork:// URL or no art — check if file already exists on disk
      if (art && art.startsWith('artwork://')) {
        // Already persisted — find existing artFile
        const existingAlbum = store.get('albums', []).find((a) => a.id === album.id)
        if (existingAlbum?.artFile) {
          return { ...rest, artFile: existingAlbum.artFile }
        }
      }

      return rest
    })

    store.set('albums', /** @type {PersistedAlbum[]} */ (persistedAlbums))
    store.set('tracks', tracks)
    store.set('folders', folders || [])
  } catch (err) {
    console.error('Failed to save library:', err)
  }
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

    return { albums: /** @type {Album[]} */ (hydratedAlbums), tracks, folders }
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

    // Remove all artwork files
    if (fs.existsSync(artworkDir)) {
      const files = fs.readdirSync(artworkDir)
      for (const file of files) {
        fs.unlinkSync(path.join(artworkDir, file))
      }
    }
  } catch (err) {
    console.error('Failed to clear library:', err)
  }
}
