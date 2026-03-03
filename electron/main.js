// @ts-check

import { app, BrowserWindow, dialog, ipcMain, protocol, net, session, shell } from 'electron'
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

// Windows SMTC resolves the app display name from a Start Menu shortcut
// linked to the AppUserModelId. Without one, it shows "Unknown App".
function ensureStartMenuShortcut() {
  if (process.platform !== 'win32') return
  const shortcutPath = path.join(
    app.getPath('appData'),
    'Microsoft', 'Windows', 'Start Menu', 'Programs', 'Cymaveil.lnk'
  )
  const shortcutDetails = {
    target: process.execPath,
    appUserModelId: APP_USER_MODEL_ID,
    description: 'Cymaveil Music Player',
  }
  try {
    // Update if it already exists (exe path may have changed), otherwise create
    if (fs.existsSync(shortcutPath)) {
      shell.writeShortcutLink(shortcutPath, 'update', shortcutDetails)
    } else {
      shell.writeShortcutLink(shortcutPath, 'create', shortcutDetails)
    }
  } catch {
    // Non-critical — SMTC will just show "Unknown App"
  }
}

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

// ── Screenshot mode helpers ─────────────────────────────────────────────────

/** @type {{ albums: import('./types').Album[], tracks: import('./types').Track[], folders: string[] } | null} */
let screenshotMockData = null
/** @type {string[]} */
let screenshotArtworkFiles = []
/** @type {string | null} */
let screenshotWavPath = null

/**
 * Generate a pink noise WAV file using the Voss-McCartney algorithm.
 * Produces natural bass-heavy spectrum that looks good on visualizer bars.
 * @param {string} filePath
 * @param {number} durationSec
 * @param {number} [sampleRate=44100]
 */
function generatePinkNoiseWav(filePath, durationSec, sampleRate = 44100) {
  const numSamples = Math.floor(sampleRate * durationSec)
  const numRows = 12 // number of octave rows
  const rows = new Float64Array(numRows)
  let runningSum = 0

  const dataSize = numSamples * 2 // 16-bit mono
  const header = Buffer.alloc(44)
  // RIFF header
  header.write('RIFF', 0)
  header.writeUInt32LE(36 + dataSize, 4)
  header.write('WAVE', 8)
  // fmt chunk
  header.write('fmt ', 12)
  header.writeUInt32LE(16, 16)       // chunk size
  header.writeUInt16LE(1, 20)        // PCM
  header.writeUInt16LE(1, 22)        // mono
  header.writeUInt32LE(sampleRate, 24)
  header.writeUInt32LE(sampleRate * 2, 28) // byte rate
  header.writeUInt16LE(2, 32)        // block align
  header.writeUInt16LE(16, 34)       // bits per sample
  // data chunk
  header.write('data', 36)
  header.writeUInt32LE(dataSize, 40)

  const samples = Buffer.alloc(dataSize)
  const maxVal = 32767
  // Pre-fill rows with random values
  for (let i = 0; i < numRows; i++) {
    rows[i] = (Math.random() * 2 - 1)
    runningSum += rows[i]
  }

  for (let i = 0; i < numSamples; i++) {
    // Voss-McCartney: use trailing zeros of counter to decide which row to update
    let k = i
    let tz = 0
    if (k > 0) {
      while ((k & 1) === 0) { tz++; k >>= 1 }
    }
    const row = tz % numRows
    runningSum -= rows[row]
    rows[row] = (Math.random() * 2 - 1)
    runningSum += rows[row]

    // Normalize: sum of numRows random [-1,1] values → divide by numRows, then add white noise
    const pink = (runningSum / numRows + (Math.random() * 2 - 1)) * 0.5
    const clamped = Math.max(-1, Math.min(1, pink))
    samples.writeInt16LE(Math.round(clamped * maxVal), i * 2)
  }

  fs.writeFileSync(filePath, Buffer.concat([header, samples]))
}

const SCREENSHOT_ALBUM_META = [
  { title: 'Depth of Field', artist: 'The Foreground Masks' },
  { title: 'Glass Resonance', artist: 'Cymaveil' },
  { title: 'Mosaic Drift', artist: 'The Tile Transitions' },
  { title: 'Full Surface', artist: 'Contour & Bass' },
  { title: 'Living Art', artist: 'The Segmentation Faults' },
  { title: 'Ambient Glow', artist: 'Pink Noise Ensemble' },
  { title: 'Veil of Sound', artist: 'The Waveforms' },
  { title: 'Radial Burst', artist: 'Vinyl Surface' },
]

/**
 * Read art files from docs/screenshots/art/, copy to userData/artwork/,
 * generate a WAV, and build mock LibraryData.
 */
function setupScreenshotData() {
  const artDir = path.join(__dirname, '..', 'docs', 'screenshots', 'art')
  if (!fs.existsSync(artDir)) {
    console.error('[screenshot] docs/screenshots/art/ directory not found')
    app.quit()
    return
  }

  const artFiles = fs.readdirSync(artDir).filter((f) => /\.(jpe?g|png|webp)$/i.test(f))
  if (artFiles.length === 0) {
    console.error('[screenshot] No image files found in docs/screenshots/art/')
    app.quit()
    return
  }

  // Copy art to userData/artwork/ as screenshot-N.ext
  const artworkDir = path.join(app.getPath('userData'), 'artwork')
  fs.mkdirSync(artworkDir, { recursive: true })

  /** @type {import('./types').Album[]} */
  const albums = []
  /** @type {import('./types').Track[]} */
  const tracks = []

  // Generate pink noise WAV
  screenshotWavPath = path.join(app.getPath('temp'), 'cymaveil-screenshot-pink.wav')
  generatePinkNoiseWav(screenshotWavPath, 120)

  for (let i = 0; i < artFiles.length; i++) {
    const srcFile = artFiles[i]
    const ext = path.extname(srcFile).toLowerCase()
    const destName = `screenshot-${i}${ext}`
    const destPath = path.join(artworkDir, destName)

    fs.copyFileSync(path.join(artDir, srcFile), destPath)
    screenshotArtworkFiles.push(destPath)

    const meta = SCREENSHOT_ALBUM_META[i % SCREENSHOT_ALBUM_META.length]
    const albumId = `screenshot-${i}`

    albums.push({
      id: albumId,
      title: meta.title,
      artist: meta.artist,
      year: 2024,
      art: `artwork://file/${encodeURIComponent(destName)}`,
      dominantColor: '#1a1a2e',
      accentColor: '#e94560',
    })

    tracks.push({
      id: `screenshot-track-${i}`,
      title: meta.title,
      artist: meta.artist,
      albumId,
      duration: 120,
      trackNum: 1,
      filePath: screenshotWavPath,
    })
  }

  screenshotMockData = { albums, tracks, folders: [] }
  console.log(`[screenshot] Prepared ${albums.length} mock albums from docs/screenshots/art/`)
}

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

// ── Screenshot mode IPC interception ────────────────────────────────────────
// Registered BEFORE normal handlers so they take precedence when active.
if (isScreenshotMode) {
  ipcMain.handle('library:load', async () => screenshotMockData)
  ipcMain.handle('library:save', async () => {})
  ipcMain.handle('library:clear', async () => {})
  ipcMain.handle('playback:load', async () => ({
    currentTrackIndex: 0,
    currentTime: 0,
    playQueue: [],
    queueIndex: -1,
    shuffle: false,
    repeat: 'off',
  }))
  ipcMain.handle('playback:save', async () => {})
  ipcMain.on('playback:pushTime', () => {})

  ipcMain.handle('screenshot:capture', async (_event, theme) => {
    if (!mainWindow) return
    const image = await mainWindow.webContents.capturePage()
    const pngData = image.toPNG()
    const outDir = path.join(__dirname, '..', 'docs', 'screenshots')
    fs.mkdirSync(outDir, { recursive: true })
    const outPath = path.join(outDir, `now-playing-${theme}.png`)
    fs.writeFileSync(outPath, pngData)
    console.log(`[screenshot] Saved ${outPath}`)
  })

  ipcMain.handle('screenshot:combine', async () => {
    const { spawn } = await import('child_process')
    const scriptPath = path.join(__dirname, '..', 'docs', 'screenshots', 'art', 'combine_themes.py')
    return new Promise((resolve) => {
      const proc = spawn('python3', [scriptPath], { stdio: 'inherit' })
      proc.on('close', () => {
        // Cleanup: delete copied artwork files
        for (const f of screenshotArtworkFiles) {
          try { fs.unlinkSync(f) } catch {}
        }
        // Delete temp WAV
        if (screenshotWavPath) {
          try { fs.unlinkSync(screenshotWavPath) } catch {}
        }
        console.log('[screenshot] Cleanup complete')
        resolve(undefined)
        app.quit()
      })
      proc.on('error', (err) => {
        console.error('[screenshot] Failed to run combine script:', err.message)
        resolve(undefined)
        app.quit()
      })
    })
  })
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

app.whenReady().then(() => {
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
  if (isScreenshotMode) setupScreenshotData()

  // Create window first for fastest first paint
  createWindow()

  ensureStartMenuShortcut()

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
