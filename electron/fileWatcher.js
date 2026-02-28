// @ts-check

/** @typedef {import('./types').WatcherEvent} WatcherEvent */

import chokidar from 'chokidar'
import path from 'path'
import { AUDIO_EXTENSIONS } from './musicScanner.js'

/** @type {import('chokidar').FSWatcher | null} */
let watcher = null

/**
 * Start watching folders recursively for audio file additions/deletions.
 * @param {string[]} folders - Array of folder paths to watch
 * @param {(event: WatcherEvent) => void} onEvent - Callback for file events
 */
export function startWatching(folders, onEvent) {
  stopWatching()

  if (!folders || folders.length === 0) return

  watcher = chokidar.watch(folders, {
    ignoreInitial: true,
    persistent: true,
    awaitWriteFinish: {
      stabilityThreshold: 500,
      pollInterval: 100,
    },
    // Only watch directories and audio files
    ignored: (filePath, stats) => {
      if (!stats) return false // allow directory traversal
      if (stats.isDirectory()) return false
      const ext = path.extname(filePath).toLowerCase()
      return !AUDIO_EXTENSIONS.has(ext)
    },
  })

  watcher.on('add', (filePath) => {
    onEvent({ type: 'add', filePath })
  })

  watcher.on('unlink', (filePath) => {
    onEvent({ type: 'unlink', filePath })
  })

  watcher.on('error', (err) => {
    console.error('[fileWatcher] error:', err)
  })
}

/**
 * Stop watching all folders and clean up.
 * Nulls the reference immediately so no new events fire,
 * then closes the underlying native watchers in the background.
 * @returns {void}
 */
export function stopWatching() {
  if (watcher) {
    const w = watcher
    watcher = null
    w.close()
  }
}
