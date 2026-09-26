import { useState, useCallback, useEffect, useRef, useMemo } from 'react'
import type { Album, Track, ScanProgress, WatcherEvent } from '../types'
import { selectAndImportFolder } from '../lib/musicLibrary'

/** True when filePath is inside folder — a bare startsWith would also match "Album 2" for "Album". */
function isInFolder(filePath: string, folder: string): boolean {
  if (!filePath.startsWith(folder)) return false
  if (/[\\/]$/.test(folder)) return true
  const next = filePath[folder.length]
  return next === '/' || next === '\\'
}

/**
 * Add scanned tracks to the library. Tracks match on file path and albums on title,
 * not ID: libraries scanned before IDs were stable hold `imported-N` IDs that playlists
 * reference, so existing entries keep theirs and new tracks join the existing album.
 * Returns the input arrays untouched when nothing is new.
 */
function mergeScanned(
  albums: Album[],
  tracks: Track[],
  scanned: { album: Album; track: Track }[]
): { albums: Album[]; tracks: Track[] } {
  const albumIdByTitle = new Map(albums.map((a) => [a.title, a.id]))
  const knownPaths = new Set(tracks.map((t) => t.filePath))
  // Cover of each album added by this merge, keyed by title
  const addedArt = new Map<string, string | null>()
  const newAlbums: Album[] = []
  const newTracks: Track[] = []

  for (const { album, track } of scanned) {
    if (knownPaths.has(track.filePath)) continue
    knownPaths.add(track.filePath)

    let albumId = albumIdByTitle.get(album.title)
    if (!albumId) {
      albumId = album.id
      albumIdByTitle.set(album.title, albumId)
      addedArt.set(album.title, album.art)
      newAlbums.push(album)
    }
    // Single-file scans hand back the track's art on both album and track; when the
    // album is new here it already carries that cover, so drop the per-track copy.
    const art = track.art && addedArt.get(album.title) === track.art ? null : track.art
    newTracks.push({ ...track, albumId, art })
  }

  if (newTracks.length === 0) return { albums, tracks }
  return { albums: [...albums, ...newAlbums], tracks: [...tracks, ...newTracks] }
}

export default function useLibrary() {
  const [albums, setAlbums] = useState<Album[]>([])
  const [tracks, setTracks] = useState<Track[]>([])
  const [folders, setFolders] = useState<string[]>([])
  const [isLoading, setIsLoading] = useState<boolean>(true)
  const [isScanning, setIsScanning] = useState<boolean>(false)
  const [scanError, setScanError] = useState<string | null>(null)
  const [scanProgress, setScanProgress] = useState<ScanProgress | null>(null)

  // Refs to prevent saving the initial empty state or re-persisting freshly loaded data
  const hasLoadedRef = useRef<boolean>(false)
  const saveTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null)
  const savedFoldersRef = useRef(folders)
  const isScanningRef = useRef<boolean>(false)
  const tracksRef = useRef(tracks)
  tracksRef.current = tracks
  const albumsRef = useRef(albums)
  albumsRef.current = albums
  const foldersRef = useRef(folders)
  foldersRef.current = folders

  // Update refs along with state, so an event handled before React re-renders (a burst
  // of watcher events when an album is copied in or deleted) builds on this change
  // instead of overwriting it.
  const commitLibrary = useCallback((nextAlbums: Album[], nextTracks: Track[]) => {
    albumsRef.current = nextAlbums
    tracksRef.current = nextTracks
    setAlbums(nextAlbums)
    setTracks(nextTracks)
  }, [])

  /** Drop tracks failing `keep`, and any album left without tracks */
  const keepTracks = useCallback((keep: (t: Track) => boolean) => {
    const remaining = tracksRef.current.filter(keep)
    const albumIdsWithTracks = new Set(remaining.map((t) => t.albumId))
    commitLibrary(albumsRef.current.filter((a) => albumIdsWithTracks.has(a.id)), remaining)
  }, [commitLibrary])

  // Load persisted library on mount, then reconcile with filesystem
  useEffect(() => {
    async function load() {
      if (!window.electronAPI?.loadLibrary) {
        setIsLoading(false)
        return
      }

      try {
        const data = await window.electronAPI.loadLibrary()
        let albums = data.albums
        let tracks = data.tracks
        const loadedFolders = data.folders || []

        // Reconcile: detect files added/removed while the app was closed
        if (loadedFolders.length > 0 && window.electronAPI.reconcileLibrary) {
          try {
            const existingPaths = tracks.map((t) => t.filePath)
            const { added, removedPaths } = await window.electronAPI.reconcileLibrary(loadedFolders, existingPaths)

            if (removedPaths.length > 0) {
              const removedSet = new Set(removedPaths)
              tracks = tracks.filter((t) => !removedSet.has(t.filePath))
              const albumIdsWithTracks = new Set(tracks.map((t) => t.albumId))
              albums = albums.filter((a) => albumIdsWithTracks.has(a.id))
            }

            if (added.length > 0) {
              ({ albums, tracks } = mergeScanned(albums, tracks, added))
            }
          } catch (err) {
            console.error('Failed to reconcile library:', err)
          }
        }

        // Keep folders even if every track in them is gone, so they stay removable.
        // An all-empty load (fresh install or read failure) sets nothing and so never
        // triggers a save that would overwrite the file.
        if (albums.length > 0 || tracks.length > 0 || loadedFolders.length > 0) {
          setAlbums(albums)
          setTracks(tracks)
          setFolders(loadedFolders)
        }
      } catch (err) {
        console.error('Failed to load library:', err)
      } finally {
        // Mark as loaded so the save effect knows it can start persisting
        hasLoadedRef.current = true
        setIsLoading(false)
      }
    }

    load()
  }, [])

  // Auto-save when albums/tracks/folders change (debounced unless folders changed)
  useEffect(() => {
    // Don't save until initial load is complete
    if (!hasLoadedRef.current) return
    if (!window.electronAPI?.saveLibrary) return

    // No empty-library guard here: hasLoadedRef already blocks the pre-load mount, and
    // removing the last folder must persist an empty library or it comes back on restart.
    clearTimeout(saveTimerRef.current!)
    const save = () => {
      window.electronAPI!.saveLibrary({ albums, tracks, folders }).then((artUpdates) => {
        // When base64 data URIs are externalized to artwork:// URLs on disk,
        // update in-memory albums/tracks so caches (e.g. segmentation) use
        // stable keys that will match on the next app launch.
        if (!artUpdates) return
        if (Object.keys(artUpdates.albums).length > 0) {
          setAlbums((prev) =>
            prev.map((a) => {
              const newArt = artUpdates.albums[a.id]
              return newArt ? { ...a, art: newArt } : a
            })
          )
        }
        if (Object.keys(artUpdates.tracks).length > 0) {
          setTracks((prev) =>
            prev.map((t) => {
              const newArt = artUpdates.tracks[t.id]
              return newArt ? { ...t, art: newArt } : t
            })
          )
        }
      }).catch((err: unknown) => {
        console.error('Failed to save library:', err)
      })
    }

    // Adding or removing a folder is rare and should survive quitting straight
    // afterwards, so skip the debounce. Watcher and art updates still batch up.
    if (folders !== savedFoldersRef.current) {
      savedFoldersRef.current = folders
      save()
      return
    }

    saveTimerRef.current = setTimeout(save, 500)
    return () => clearTimeout(saveTimerRef.current!)
  }, [albums, tracks, folders])

  const albumMap = useMemo(
    () => new Map(albums.map((a) => [a.id, a])),
    [albums]
  )

  const trackMap = useMemo(
    () => new Map(tracks.map((t) => [t.id, t])),
    [tracks]
  )

  /** O(1) lookup — use instead of tracks.find() when resolving lists of IDs */
  const getTrack = useCallback(
    (id: string): Track | undefined => trackMap.get(id),
    [trackMap]
  )

  const getAlbumForTrack = useCallback(
    (track: Track | null): Album | null => albumMap.get(track?.albumId ?? '') ?? null,
    [albumMap]
  )

  const getTracksForAlbum = useCallback(
    (albumId: string): Track[] => tracks.filter((t) => t.albumId === albumId).sort((a, b) => a.trackNum - b.trackNum),
    [tracks]
  )

  const importFolder = useCallback(async (): Promise<boolean> => {
    if (isScanningRef.current) return false
    isScanningRef.current = true
    setIsScanning(true)
    setScanError(null)
    setScanProgress(null)

    // Subscribe to progress events from the main process
    const unsubscribe = window.electronAPI?.onScanProgress?.((data: ScanProgress) => {
      setScanProgress({ current: data.current, total: data.total })
    })

    try {
      const result = await selectAndImportFolder()

      if (!result) {
        // User cancelled or browser mode
        return false
      }

      if (result.albums.length === 0) {
        setScanError('No audio files found in the selected folder.')
        return false
      }

      // Merge new imports into existing library (additive)
      const scannedAlbums = new Map(result.albums.map((a) => [a.id, a]))
      const merged = mergeScanned(
        albumsRef.current,
        tracksRef.current,
        result.tracks.map((track) => ({ album: scannedAlbums.get(track.albumId)!, track }))
      )
      commitLibrary(merged.albums, merged.tracks)

      // Track the imported folder path
      if (result.folderPath) {
        setFolders((prev) => {
          if (prev.includes(result.folderPath)) return prev
          return [...prev, result.folderPath]
        })
      }

      return true
    } catch (err: unknown) {
      setScanError((err as Error).message || 'Failed to scan folder.')
      return false
    } finally {
      isScanningRef.current = false
      setIsScanning(false)
      setScanProgress(null)
      unsubscribe?.()
    }
  }, [commitLibrary])

  const removeFolder = useCallback((folderPath: string) => {
    // Tracks still covered by another imported folder (e.g. a parent) stay.
    const otherFolders = foldersRef.current.filter((f) => f !== folderPath)
    keepTracks((t) => !isInFolder(t.filePath, folderPath) || otherFolders.some((f) => isInFolder(t.filePath, f)))
    setFolders(otherFolders)
  }, [keepTracks])

  // File watcher — start/stop when folders change
  useEffect(() => {
    if (!window.electronAPI?.startWatching) return
    if (!hasLoadedRef.current) return
    if (folders.length === 0) {
      window.electronAPI.stopWatching()
      return
    }

    window.electronAPI.startWatching(folders)

    const unsubscribe = window.electronAPI.onWatcherEvent((event: WatcherEvent) => {
      if (event.type === 'add') {
        window.electronAPI!.scanSingleFile(event.filePath).then((result) => {
          const merged = mergeScanned(albumsRef.current, tracksRef.current, [result])
          commitLibrary(merged.albums, merged.tracks)
        }).catch((err: unknown) => {
          console.error('Failed to scan new file:', err)
        })
      } else if (event.type === 'unlink') {
        keepTracks((t) => t.filePath !== event.filePath)
      }
    })

    return () => {
      unsubscribe()
      window.electronAPI!.stopWatching()
    }
  }, [folders, commitLibrary, keepTracks])

  return {
    albums,
    tracks,
    folders,
    isLoading,
    isScanning,
    scanError,
    scanProgress,
    getTrack,
    getAlbumForTrack,
    getTracksForAlbum,
    importFolder,
    removeFolder,
  }
}
