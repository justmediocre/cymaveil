// @ts-check

import { app, BrowserWindow, dialog, ipcMain, protocol, net, session } from 'electron'
import path from 'path'
import fs from 'fs'
import { fileURLToPath } from 'url'

// Heavy modules (music-metadata, chokidar, electron-store) are lazy-loaded
// so they don't block the main process before the window can be created.
/** @type {typeof import('./musicScanner.js') | undefined} */
let _scanner
/** @type {typeof import('./fileWatcher.js') | undefined} */
let _watcher
/** @type {typeof import('./libraryStore.js') | undefined} */
let _store
/** @type {typeof import('./playlistFile.js') | undefined} */
let _playlistFile
/** @type {typeof import('./maskOverrideFile.js') | undefined} */
let _maskOverrideFile
/** @type {typeof import('./updateChecker.js') | undefined} */
let _updateChecker
const getScanner = async () => (_scanner ??= await import('./musicScanner.js'))
const getWatcher = async () => (_watcher ??= await import('./fileWatcher.js'))
const getStore = async () => (_store ??= await import('./libraryStore.js'))
const getPlaylistFile = async () => (_playlistFile ??= await import('./playlistFile.js'))
const getMaskOverrideFile = async () => (_maskOverrideFile ??= await import('./maskOverrideFile.js'))
const getUpdateChecker = async () => (_updateChecker ??= await import('./updateChecker.js'))

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const isDev = !app.isPackaged
const isScreenshotMode = isDev && process.argv.includes('--screenshot')

// Set Windows app identity for SMTC (System Media Transport Controls)
const APP_USER_MODEL_ID = 'com.cymaveil.player'
app.setAppUserModelId(APP_USER_MODEL_ID)
app.setName('Cymaveil')

// Register custom schemes as privileged before app is ready.
// bypassCSP is required so the renderer can load audio/artwork from these
// custom protocols despite the strict session-level CSP. This is safe because
// the protocol handlers validate file types and paths before serving content.
protocol.registerSchemesAsPrivileged([
  {
    scheme: 'audio',
    privileges: {
      stream: true,
      supportFetchAPI: true,
      bypassCSP: true,
      corsEnabled: true,
    },
  },
  {
    scheme: 'artwork',
    privileges: {
      supportFetchAPI: true,
      bypassCSP: true,
      corsEnabled: true,
    },
  },
])

/** @type {BrowserWindow | null} */
let mainWindow = null
/** @type {number | null} */
let latestPlaybackTime = null

function createWindow() {
  const preloadPath = path.join(__dirname, 'preload.cjs')

  mainWindow = new BrowserWindow({
    title: 'Cymaveil',
    width: isScreenshotMode ? 900 : 1200,
    height: isScreenshotMode ? 650 : 800,
    minWidth: 900,
    minHeight: 650,
    frame: false,
    titleBarStyle: 'hidden',
    trafficLightPosition: { x: 16, y: 16 },
    backgroundColor: '#0a0a0b',
    icon: path.join(__dirname, '..', 'build', 'icon.png'),
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
      partition: 'persist:cymaveil',
      preload: preloadPath,
      additionalArguments: isScreenshotMode ? ['--screenshot'] : [],
    },
  })

  // Intercept OS-level close (Alt+F4, taskbar close, etc.) and destroy
  // immediately to skip Chromium's slow renderer/GPU teardown
  mainWindow.on('close', (e) => {
    e.preventDefault()
    flushPlaybackTime()
    _watcher?.stopWatching()
    mainWindow?.destroy()
  })

  mainWindow.on('closed', () => {
    mainWindow = null
  })

  // Keep window title as "Cymaveil" for Windows SMTC display name
  mainWindow.on('page-title-updated', (e) => {
    e.preventDefault()
  })

  mainWindow.on('enter-full-screen', () => {
    mainWindow?.webContents.send('fullscreen:changed', true)
  })
  mainWindow.on('leave-full-screen', () => {
    mainWindow?.webContents.send('fullscreen:changed', false)
  })

  // Block all navigations away from the app (SPA — no page navigations should occur)
  mainWindow.webContents.on('will-navigate', (event, url) => {
    if (isDev && url.startsWith('http://localhost:')) return
    event.preventDefault()
  })

  // Block all new window requests (e.g. target="_blank" links)
  mainWindow.webContents.setWindowOpenHandler(() => ({ action: 'deny' }))

  if (isDev) {
    mainWindow.loadURL('http://localhost:5173')
  } else {
    mainWindow.loadFile(path.join(__dirname, '../dist/index.html'))
  }
}

/**
 * Validate that a value is a safe absolute filesystem path
 * @param {unknown} input
 * @returns {asserts input is string}
 */
function validatePath(input) {
  if (typeof input !== 'string' || input.length === 0) {
    throw new Error('Path must be a non-empty string')
  }
  if (!path.isAbsolute(input)) {
    throw new Error('Path must be absolute')
  }
  if (input.includes('\0')) {
    throw new Error('Path contains null bytes')
  }
}

// Screenshot mode IPC — registered BEFORE normal handlers so they take precedence.
if (isScreenshotMode) {
  const { registerScreenshotIPC } = await import('./screenshot.js')
  registerScreenshotIPC(() => mainWindow)
}

// IPC: Open native folder picker
ipcMain.handle('dialog:selectFolder', async () => {
  const win = mainWindow || BrowserWindow.getFocusedWindow()
  const result = await dialog.showOpenDialog(/** @type {BrowserWindow} */ (win), {
    properties: ['openDirectory'],
    title: 'Select Music Folder',
  })
  if (result.canceled || result.filePaths.length === 0) return null
  return result.filePaths[0]
})

// IPC: Scan a folder for music files
ipcMain.handle('music:scanFolder', async (event, folderPath) => {
  validatePath(folderPath)
  const { scanMusicFolder } = await getScanner()
  return await scanMusicFolder(folderPath, ({ current, total }) => {
    event.sender.send('music:scanProgress', { current, total })
  })
})

// IPC: Library persistence (skipped in screenshot mode — intercepted above)
if (!isScreenshotMode) {
  ipcMain.handle('library:load', async () => {
    const { loadLibrary } = await getStore()
    return loadLibrary()
  })

  ipcMain.handle('library:save', async (_event, data) => {
    if (!data || !Array.isArray(data.albums) || !Array.isArray(data.tracks)) return
    const { saveLibrary } = await getStore()
    saveLibrary(data)
  })

  ipcMain.handle('library:clear', async () => {
    const { clearLibrary } = await getStore()
    clearLibrary()
  })

  // IPC: Playback state persistence
  ipcMain.handle('playback:load', async () => {
    const { loadPlaybackState } = await getStore()
    return loadPlaybackState()
  })

  ipcMain.handle('playback:save', async (_event, data) => {
    if (!data || typeof data.currentTrackIndex !== 'number' || typeof data.currentTime !== 'number') return
    const { savePlaybackState } = await getStore()
    savePlaybackState(data)
  })

  // Fire-and-forget time updates from renderer (no response needed)
  ipcMain.on('playback:pushTime', (_event, time) => {
    if (typeof time !== 'number') return
    latestPlaybackTime = time
  })
}

/** Merge cached currentTime into the persisted playback state */
function flushPlaybackTime() {
  if (latestPlaybackTime === null || !_store) return
  const saved = _store.loadPlaybackState()
  saved.currentTime = latestPlaybackTime
  _store.savePlaybackState(saved)
  latestPlaybackTime = null
}

// IPC: Playlist persistence
ipcMain.handle('playlists:load', async () => {
  const { loadPlaylists } = await getStore()
  return loadPlaylists()
})

ipcMain.handle('playlists:save', async (_event, data) => {
  if (!Array.isArray(data)) return
  const { savePlaylists } = await getStore()
  savePlaylists(data)
})

ipcMain.handle('playlists:export', async (_event, playlist, tracks) => {
  const win = /** @type {BrowserWindow} */ (mainWindow || BrowserWindow.getFocusedWindow())
  const { exportPlaylistToM3u8 } = await getPlaylistFile()
  return await exportPlaylistToM3u8(win, playlist, tracks)
})

ipcMain.handle('playlists:import', async () => {
  const win = /** @type {BrowserWindow} */ (mainWindow || BrowserWindow.getFocusedWindow())
  const { importPlaylistFromM3u8 } = await getPlaylistFile()
  return await importPlaylistFromM3u8(win)
})

ipcMain.handle('maskOverrides:export', async (_event, jsonData) => {
  if (typeof jsonData !== 'string') return
  const win = /** @type {BrowserWindow} */ (mainWindow || BrowserWindow.getFocusedWindow())
  const { exportMaskOverrides } = await getMaskOverrideFile()
  return await exportMaskOverrides(win, jsonData)
})

ipcMain.handle('maskOverrides:import', async () => {
  const win = /** @type {BrowserWindow} */ (mainWindow || BrowserWindow.getFocusedWindow())
  const { importMaskOverrides } = await getMaskOverrideFile()
  return await importMaskOverrides(win)
})

// IPC: File watcher
ipcMain.handle('watcher:start', async (_event, folders) => {
  if (!Array.isArray(folders)) {
    throw new Error('folders must be an array')
  }
  for (const f of folders) validatePath(f)
  const { startWatching } = await getWatcher()
  startWatching(folders, (watcherEvent) => {
    mainWindow?.webContents.send('watcher:event', watcherEvent)
  })
})

ipcMain.handle('watcher:stop', async () => {
  const { stopWatching } = await getWatcher()
  stopWatching()
})

// IPC: Reconcile library with filesystem on startup
// Finds new files added while the app was closed and files that were removed
ipcMain.handle('music:reconcile', async (_event, folders, existingFilePaths) => {
  if (!Array.isArray(folders) || !Array.isArray(existingFilePaths)) {
    throw new Error('folders and existingFilePaths must be arrays')
  }
  for (const f of folders) validatePath(f)

  const { findAudioFiles, scanSingleFile } = await getScanner()

  // Gather all audio files currently on disk across all watched folders
  const diskFiles = new Set(
    (await Promise.all(folders.map((f) => findAudioFiles(f)))).flat()
  )

  const existingSet = new Set(existingFilePaths)

  // Files on disk but not in the library → added while app was closed
  const addedPaths = [...diskFiles].filter((f) => !existingSet.has(f))

  // Files in the library but no longer on disk → removed while app was closed
  const removedPaths = existingFilePaths.filter((f) => !diskFiles.has(f))

  // Scan new files
  const added = await Promise.all(addedPaths.map((f) => scanSingleFile(f)))

  return { added, removedPaths }
})

// IPC: Single file scan
ipcMain.handle('music:scanFile', async (_event, filePath) => {
  validatePath(filePath)
  const { AUDIO_EXTENSIONS, scanSingleFile } = await getScanner()
  const ext = path.extname(filePath).toLowerCase()
  if (!AUDIO_EXTENSIONS.has(ext)) {
    throw new Error(`Not an audio file: ${ext}`)
  }
  return await scanSingleFile(filePath)
})

// IPC: Window controls
ipcMain.handle('window:minimize', () => mainWindow?.minimize())
ipcMain.handle('window:maximize', () => {
  if (mainWindow?.isMaximized()) {
    mainWindow.unmaximize()
  } else {
    mainWindow?.maximize()
  }
})
ipcMain.handle('window:close', () => {
  flushPlaybackTime()
  _watcher?.stopWatching()
  mainWindow?.destroy()
})
ipcMain.handle('window:toggleFullscreen', () => {
  if (!mainWindow) return
  mainWindow.setFullScreen(!mainWindow.isFullScreen())
})
ipcMain.handle('window:isFullscreen', () => mainWindow?.isFullScreen() ?? false)

// IPC: Update checker
ipcMain.handle('update:check', async () => {
  const uc = await getUpdateChecker()
  const info = await uc.checkForUpdate(app.getVersion())
  if (info && !uc.isDismissed(info.version)) {
    mainWindow?.webContents.send('update:available', info)
  }
  return info
})

ipcMain.handle('update:dismiss', async (_event, version) => {
  if (typeof version !== 'string') return
  const uc = await getUpdateChecker()
  uc.dismissVersion(version)
})

ipcMain.handle('update:openRelease', async (_event, url) => {
  if (typeof url !== 'string') return
  const uc = await getUpdateChecker()
  uc.openReleasePage(url)
})

ipcMain.handle('update:getEnabled', async () => {
  const uc = await getUpdateChecker()
  return uc.getUpdateCheckEnabled()
})

ipcMain.handle('update:setEnabled', async (_event, enabled) => {
  if (typeof enabled !== 'boolean') return
  const uc = await getUpdateChecker()
  uc.setUpdateCheckEnabled(enabled)
})

const gotTheLock = app.requestSingleInstanceLock()
if (!gotTheLock) {
  app.quit()
} else {

app.on('second-instance', () => {
  if (mainWindow) {
    if (mainWindow.isMinimized()) mainWindow.restore()
    mainWindow.focus()
  }
})

app.whenReady().then(async () => {
  // Get the partition session used by the BrowserWindow — protocol handlers
  // and CSP must be registered on this session, not session.defaultSession.
  const ses = session.fromPartition('persist:cymaveil')

  // Apply Content Security Policy via session headers (covers all responses including dev server)
  ses.webRequest.onHeadersReceived((details, callback) => {
    const scriptSrc = isDev
      ? "'self' 'unsafe-inline' 'wasm-unsafe-eval' https://cdn.jsdelivr.net"
      : "'self' 'wasm-unsafe-eval' https://cdn.jsdelivr.net"
    const connectSrc = isDev
      ? "'self' blob: https://huggingface.co https://*.hf.co https://cdn.jsdelivr.net ws://localhost:*"
      : "'self' blob: https://huggingface.co https://*.hf.co https://cdn.jsdelivr.net"
    const csp = [
      "default-src 'none'",
      `script-src ${scriptSrc}`,
      "style-src 'self' 'unsafe-inline'",
      "img-src 'self' blob: data:",
      "font-src 'self' data:",
      `connect-src ${connectSrc}`,
      "media-src 'self'",
      "worker-src 'self' blob:",
      "base-uri 'self'",
      "form-action 'none'",
      "frame-ancestors 'none'",
      "object-src 'none'",
    ].join('; ')
    callback({
      responseHeaders: {
        ...details.responseHeaders,
        'Content-Security-Policy': [csp],
      },
    })
  })

  // Screenshot mode: prepare mock data before creating window
  if (isScreenshotMode) {
    const { setupScreenshotData } = await import('./screenshot.js')
    setupScreenshotData()
  }

  // Create window first for fastest first paint
  createWindow()

  // Handle audio:// protocol — serves local audio files with range request
  // support so the <audio> element can seek
  /** @type {Record<string, string>} */
  const audioMimeTypes = {
    '.mp3': 'audio/mpeg',
    '.flac': 'audio/flac',
    '.wav': 'audio/wav',
    '.ogg': 'audio/ogg',
    '.m4a': 'audio/mp4',
    '.aac': 'audio/aac',
    '.opus': 'audio/opus',
    '.wma': 'audio/x-ms-wma',
  }

  ses.protocol.handle('audio', async (request) => {
    // URL format: audio://track/<encoded-file-path>
    const url = new URL(request.url)
    const filePath = decodeURIComponent(url.pathname.replace(/^\/+/, ''))

    // Only serve known audio file types
    const ext = path.extname(filePath).toLowerCase()
    const contentType = audioMimeTypes[ext]
    if (!contentType) {
      return new Response('Forbidden — not an audio file', { status: 403 })
    }

    try {
      const stat = await fs.promises.stat(filePath)
      const fileSize = stat.size

      const rangeHeader = request.headers.get('Range')
      if (rangeHeader) {
        const match = rangeHeader.match(/bytes=(\d+)-(\d*)/)
        if (match) {
          const start = parseInt(match[1], 10)
          const end = match[2] ? parseInt(match[2], 10) : fileSize - 1
          const chunkSize = end - start + 1
          const stream = fs.createReadStream(filePath, { start, end })

          // @ts-expect-error Electron accepts Node ReadStream as Response body
          return new Response(stream, {
            status: 206,
            headers: {
              'Content-Type': contentType,
              'Content-Length': String(chunkSize),
              'Content-Range': `bytes ${start}-${end}/${fileSize}`,
              'Accept-Ranges': 'bytes',
            },
          })
        }
      }

      // No range — return the full file
      const stream = fs.createReadStream(filePath)
      // @ts-expect-error Electron accepts Node ReadStream as Response body
      return new Response(stream, {
        status: 200,
        headers: {
          'Content-Type': contentType,
          'Content-Length': String(fileSize),
          'Accept-Ranges': 'bytes',
        },
      })
    } catch (/** @type {any} */ err) {
      const status = err.code === 'ENOENT' ? 404 : 500
      const message = err.code === 'ENOENT' ? 'File not found' : 'Failed to read audio file'
      return new Response(message, { status })
    }
  })

  // Handle artwork:// protocol — serves album artwork from userData/artwork/
  ses.protocol.handle('artwork', async (request) => {
    const url = new URL(request.url)
    const filename = decodeURIComponent(url.pathname.replace(/^\/+/, ''))
    const { getArtworkPath } = await getStore()
    const filePath = getArtworkPath(filename)

    // getArtworkPath returns null if the filename escapes the artwork directory
    if (!filePath) {
      return new Response('Forbidden', { status: 403 })
    }

    const ext = path.extname(filename).toLowerCase()
    /** @type {Record<string, string>} */
    const mimeTypes = {
      '.jpg': 'image/jpeg',
      '.jpeg': 'image/jpeg',
      '.png': 'image/png',
      '.webp': 'image/webp',
    }
    const contentType = mimeTypes[ext] || 'image/jpeg'

    try {
      const data = await fs.promises.readFile(filePath)
      return new Response(data, {
        status: 200,
        headers: {
          'Content-Type': contentType,
          'Cache-Control': 'public, max-age=31536000, immutable',
        },
      })
    } catch (/** @type {any} */ err) {
      const status = err.code === 'ENOENT' ? 404 : 500
      const message = err.code === 'ENOENT' ? 'Not found' : 'Failed to read artwork'
      return new Response(message, { status })
    }
  })

  // Update checker — delayed startup check + periodic re-check
  async function performUpdateCheck() {
    try {
      const uc = await getUpdateChecker()
      const info = await uc.checkForUpdate(app.getVersion())
      if (info && !uc.isDismissed(info.version)) {
        mainWindow?.webContents.send('update:available', info)
      }
    } catch {
      // Non-critical — silently ignore
    }
  }

  setTimeout(async () => {
    const uc = await getUpdateChecker()
    if (uc.shouldCheck()) performUpdateCheck()
  }, 5000)

  setInterval(performUpdateCheck, 24 * 60 * 60 * 1000)

})

app.on('window-all-closed', () => {
  _watcher?.stopWatching()
  if (process.platform !== 'darwin') app.exit(0)
})

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) createWindow()
})

} // single-instance lock
