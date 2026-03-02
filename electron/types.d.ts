// Shared types mirroring src/types.ts (lines 1–80) plus main-process-only types

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
  shuffle: boolean
  repeat: 'off' | 'all' | 'one'
}

/** Library data persisted to disk */
export interface LibraryData {
  albums: Album[]
  tracks: Track[]
  folders: string[]
}

/** Scan result from musicScanner */
export interface ScanResult {
  albums: Album[]
  tracks: Track[]
}

/** Single file scan result */
export interface SingleFileScanResult {
  album: Album
  track: Track
}

/** File watcher event */
export interface WatcherEvent {
  type: 'add' | 'unlink'
  filePath: string
}

/** M3U8 import parse result */
export interface PlaylistImportResult {
  name: string
  filePaths: string[]
}

// ── Main-process-only types ──

/** Parsed metadata from a single audio file (internal scanner result) */
export interface ParsedAudioFile {
  filePath: string
  title: string
  artist: string
  album: string
  year: number | null
  trackNum: number | null
  duration: number
  artDataUri: string | null
}

/** Intermediate track shape during album assembly */
export interface AlbumBuildTrack {
  title: string
  artist: string
  duration: number
  trackNum: number | null
  filePath: string
}

/** Intermediate album shape during assembly (artists is a Set) */
export interface AlbumBuildEntry {
  id: string
  title: string
  artists: Set<string>
  year: number | null
  art: string
  dominantColor: string
  accentColor: string
  hasRealArt: boolean
  tracks: AlbumBuildTrack[]
}

/** Album shape stored on disk (artFile/artSvg instead of art) */
export interface PersistedAlbum {
  id: string
  title: string
  artist: string
  year: number | null
  dominantColor: string
  accentColor: string
  artFile?: string
  artSvg?: string
}

/** electron-store schema */
export interface StoreSchema {
  schemaVersion: number
  folders: string[]
  albums: PersistedAlbum[]
  tracks: Track[]
  playlists: Playlist[]
  playbackState?: PlaybackState
}
