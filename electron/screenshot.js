// @ts-check

import { app, ipcMain } from 'electron'
import path from 'path'
import fs from 'fs'
import { fileURLToPath } from 'url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))

// ── Module state ────────────────────────────────────────────────────────────

/** @type {{ albums: import('./types').Album[], tracks: import('./types').Track[], folders: string[] } | null} */
let mockData = null
/** @type {string[]} */
let artworkFiles = []
/** @type {string | null} */
let wavPath = null

// ── Private helpers ─────────────────────────────────────────────────────────

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

// ── Exported functions ──────────────────────────────────────────────────────

/**
 * Read art files from docs/screenshots/art/, copy to userData/artwork/,
 * generate a WAV, and build mock LibraryData.
 */
export function setupScreenshotData() {
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
  wavPath = path.join(app.getPath('temp'), 'cymaveil-screenshot-pink.wav')
  generatePinkNoiseWav(wavPath, 120)

  for (let i = 0; i < artFiles.length; i++) {
    const srcFile = artFiles[i]
    const ext = path.extname(srcFile).toLowerCase()
    const destName = `screenshot-${i}${ext}`
    const destPath = path.join(artworkDir, destName)

    fs.copyFileSync(path.join(artDir, srcFile), destPath)
    artworkFiles.push(destPath)

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
      filePath: wavPath,
    })
  }

  mockData = { albums, tracks, folders: [] }
  console.log(`[screenshot] Prepared ${albums.length} mock albums from docs/screenshots/art/`)
}

/**
 * Register all screenshot-mode IPC handlers.
 * Must be called BEFORE normal handlers so they take precedence.
 * @param {() => import('electron').BrowserWindow | null} getMainWindow
 */
export function registerScreenshotIPC(getMainWindow) {
  ipcMain.handle('library:load', async () => mockData)
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
    const mainWindow = getMainWindow()
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
        for (const f of artworkFiles) {
          try { fs.unlinkSync(f) } catch {}
        }
        // Delete temp WAV
        if (wavPath) {
          try { fs.unlinkSync(wavPath) } catch {}
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
