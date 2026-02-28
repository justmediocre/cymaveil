// @ts-check

/** @typedef {import('./types').Playlist} Playlist */
/** @typedef {import('./types').Track} Track */
/** @typedef {import('./types').PlaylistImportResult} PlaylistImportResult */

import { dialog } from 'electron'
import path from 'path'
import fs from 'fs'
import { AUDIO_EXTENSIONS } from './musicScanner.js'

/**
 * Export a playlist to an .m3u8 file.
 * Shows a save dialog and writes the file.
 * Returns the saved file path, or null if cancelled.
 * @param {import('electron').BrowserWindow} win
 * @param {Playlist} playlist
 * @param {Track[]} tracks
 * @returns {Promise<string | null>}
 */
export async function exportPlaylistToM3u8(win, playlist, tracks) {
  const result = await dialog.showSaveDialog(win, {
    title: 'Export Playlist',
    defaultPath: `${playlist.name}.m3u8`,
    filters: [{ name: 'M3U8 Playlist', extensions: ['m3u8'] }],
  })

  if (result.canceled || !result.filePath) return null

  const lines = ['#EXTM3U']
  for (const trackId of playlist.trackIds) {
    const track = tracks.find((t) => t.id === trackId)
    if (!track?.filePath) continue
    const duration = Math.round(track.duration || 0)
    const title = track.title || path.basename(track.filePath)
    lines.push(`#EXTINF:${duration},${title}`)
    lines.push(track.filePath)
  }

  fs.writeFileSync(result.filePath, lines.join('\n'), 'utf-8')
  return result.filePath
}

/**
 * Import a playlist from an .m3u8/.m3u file.
 * Shows an open dialog, parses the file, and returns { name, filePaths }.
 * Returns null if cancelled.
 * @param {import('electron').BrowserWindow} win
 * @returns {Promise<PlaylistImportResult | null>}
 */
export async function importPlaylistFromM3u8(win) {
  const result = await dialog.showOpenDialog(win, {
    title: 'Import Playlist',
    filters: [{ name: 'Playlist Files', extensions: ['m3u8', 'm3u'] }],
    properties: ['openFile'],
  })

  if (result.canceled || result.filePaths.length === 0) return null

  const playlistPath = result.filePaths[0]
  const playlistDir = path.dirname(playlistPath)
  const name = path.basename(playlistPath, path.extname(playlistPath))

  const content = fs.readFileSync(playlistPath, 'utf-8')
  const lines = content.split(/\r?\n/)

  /** @type {string[]} */
  const filePaths = []
  for (const line of lines) {
    const trimmed = line.trim()
    // Skip empty lines, comments, and extended info
    if (!trimmed || trimmed.startsWith('#')) continue

    // Resolve relative paths against the playlist file's directory
    const resolved = path.isAbsolute(trimmed)
      ? trimmed
      : path.resolve(playlistDir, trimmed)

    // Only include existing audio files
    const ext = path.extname(resolved).toLowerCase()
    if (!AUDIO_EXTENSIONS.has(ext)) continue
    if (!fs.existsSync(resolved)) continue

    filePaths.push(resolved)
  }

  return { name, filePaths }
}
