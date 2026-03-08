/** An album in the library */
export interface Album {
  id: string
  title: string
  artist: string
  year: number | null
  art: string | null
  dominantColor: string
  accentColor: string
}

/** A track in the library */
export interface Track {
  id: string
  title: string
  artist: string
  albumId: string
  duration: number
  trackNum: number
  filePath: string
}

/** A playlist (including the built-in Favorites) */
export interface Playlist {
  id: string
  name: string
  trackIds: string[]
  createdAt: number
}

/** Playback state persisted to disk */
export interface PlaybackState {
  currentTrackIndex: number
  currentTime: number
  playQueue: string[]
  queueIndex: number
  queueSource: 'none' | 'album' | 'playlist' | 'now-playing'
  shuffle: boolean
  repeat: 'off' | 'all' | 'one'
  playbackActive?: boolean
}

/** Library data persisted to disk */
export interface LibraryData {
  albums: Album[]
  tracks: Track[]
  folders: string[]
}

/** Scan progress event from main process */
export interface ScanProgress {
  current: number
  total: number
}

/** Scan result from musicScanner */
export interface ScanResult {
  albums: Album[]
  tracks: Track[]
}

/** Import result with folder path */
export interface ImportResult extends ScanResult {
  folderPath: string
}

/** File watcher event */
export interface WatcherEvent {
  type: 'add' | 'unlink'
  filePath: string
}

/** Single file scan result */
export interface SingleFileScanResult {
  album: Album
  track: Track
}

/** M3U8 import parse result */
export interface PlaylistImportResult {
  name: string
  filePaths: string[]
}

/** Update info from GitHub releases */
export interface UpdateInfo {
  version: string
  releaseUrl: string
  releaseName: string
  releaseNotes: string
  publishedAt: string
}

/** Extracted album colors */
export interface AlbumColors {
  dominant: string
  accent: string
  accentSecondary?: string
}

/** A point on a contour path (normalized 0-1 coords) */
export interface ContourPoint {
  x: number
  y: number
  nx: number
  ny: number
}

/** A single contour with horizontal span info */
export interface Contour {
  points: ContourPoint[]
  span: number
}

/** Result of contour extraction */
export interface ContourData {
  contours: Contour[]
  isFallback: boolean
}

/** Result of edge detection for one pass */
export interface EdgeResult {
  edges: Uint8Array
  width: number
  height: number
}

/** Result of ML-based image segmentation */
export interface SegmentationResult {
  foregroundMask: ImageData
  depthMap: Uint8Array | null
  width: number
  height: number
}

/** Available segmentation backends */
export type SegmentationBackend = 'none' | 'manual' | 'classical' | 'depth-anything'

/** Depth model size variants */
export type DepthModelSize = 'small' | 'base' | 'large'

/** Depth model quantization/precision */
export type DepthModelDtype = 'q4' | 'q8' | 'fp16' | 'fp32'

/** Model-level settings (changing these requires ML re-run) */
export interface MaskModelParams {
  modelSize: DepthModelSize
  modelDtype: DepthModelDtype
  inputResolution: number
}

/** Post-processing parameters for depthToMask (instant re-run from cached depth map) */
export interface MaskPostProcessParams {
  bilateralRadius: number
  bilateralSigmaRange: number
  morphCloseRadius: number
  morphOpenRadius: number
  textPromotionRadius: number
  textPromotionSensitivity: number
  edgeRefineRadius: number
  featherRadius: number
}

/** Available visualizer rendering styles */
export type VisualizerStyle = 'contour-bars' | 'full-surface' | 'radial-burst' | 'waveform' | 'mirrored-bars' | 'random'

/** Available mosaic tile transition animations */
export type MosaicTransition = 'flip' | 'shrink-grow' | 'cross-fade' | 'fade' | 'iris' | 'random'

/** Visualizer bar color modes */
export type VisualizerColorMode = 'auto' | 'white' | 'cyan' | 'magenta' | 'gold' | 'red' | 'green' | 'custom'

/** Visual settings toggles */
export interface VisualSettings {
  canvasVisualizer: boolean
  ambientGlow: boolean
  bassShake: boolean
  vinylDisc: boolean
  backgroundMosaic: boolean
  mosaicTransition: MosaicTransition
  mosaicDensity: number
  mosaicMaxTiles: number
  mosaicOpacity: number
  mosaicFlat: boolean
  glassBlur: boolean
  disableVisualsOnBattery: boolean
  depthLayerEnabled: boolean
  segmentationBackend: SegmentationBackend
  visualizerStyle: VisualizerStyle
  visualizerIntensity: number
  visualizerColorMode: VisualizerColorMode
  visualizerCustomColor: string
  maskDefaults: Partial<MaskPostProcessParams>
  /** Bumped when mask cache is cleared or imported — triggers re-evaluation */
  maskCacheVersion: number
}

/** Performance snapshot */
export interface PerfSnapshot {
  channels: Record<string, { avg: number; max: number }>
  renders: Record<string, number>
  heap: { used: number; total: number } | null
  frameBudget: { avgDelta: number; dropsPerSec: number } | null
}

/** Shape of window.electronAPI exposed by preload.cjs */
export interface ElectronAPI {
  isElectron: true
  platform: string
  selectFolder: () => Promise<string | null>
  scanMusicFolder: (folderPath: string) => Promise<ScanResult>
  loadLibrary: () => Promise<LibraryData>
  saveLibrary: (data: LibraryData) => Promise<void>
  clearLibrary: () => Promise<void>
  loadPlaybackState: () => Promise<PlaybackState>
  savePlaybackState: (data: PlaybackState) => Promise<void>
  pushPlaybackTime: (time: number) => void
  loadPlaylists: () => Promise<Playlist[]>
  savePlaylists: (data: Playlist[]) => Promise<void>
  exportPlaylist: (playlist: Playlist, tracks: Track[]) => Promise<string | null>
  importPlaylist: () => Promise<PlaylistImportResult | null>
  exportMaskOverrides: (jsonData: string) => Promise<string | null>
  importMaskOverrides: () => Promise<string | null>
  onScanProgress: (callback: (data: ScanProgress) => void) => () => void
  startWatching: (folders: string[]) => Promise<void>
  stopWatching: () => Promise<void>
  onWatcherEvent: (callback: (event: WatcherEvent) => void) => () => void
  scanSingleFile: (filePath: string) => Promise<SingleFileScanResult>
  reconcileLibrary: (folders: string[], existingFilePaths: string[]) => Promise<{ added: SingleFileScanResult[], removedPaths: string[] }>
  windowMinimize: () => Promise<void>
  windowMaximize: () => Promise<void>
  windowClose: () => Promise<void>
  windowToggleFullscreen: () => Promise<void>
  windowIsFullscreen: () => Promise<boolean>
  onFullscreenChange: (callback: (isFullscreen: boolean) => void) => () => void
  isScreenshotMode?: boolean
  screenshotCapture?: (theme: string) => Promise<void>
  screenshotCombine?: () => Promise<void>
  checkForUpdate: () => Promise<UpdateInfo | null>
  dismissUpdate: (version: string) => Promise<void>
  openReleasePage: (url: string) => Promise<void>
  getUpdateCheckEnabled: () => Promise<boolean>
  setUpdateCheckEnabled: (enabled: boolean) => Promise<void>
  onUpdateAvailable: (callback: (info: UpdateInfo) => void) => () => void
}

declare global {
  interface Window {
    electronAPI?: ElectronAPI
  }
}
