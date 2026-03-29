import { useState, useCallback, useEffect, useRef, useMemo } from 'react'
import type { Album, Track, ScanProgress, WatcherEvent } from '../types'
import { selectAndImportFolder } from '../lib/musicLibrary'

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
  const isScanningRef = useRef<boolean>(false)
  const tracksRef = useRef(tracks)
  tracksRef.current = tracks

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
              const existingTrackIds = new Set(tracks.map((t) => t.id))
              const existingAlbumIds = new Set(albums.map((a) => a.id))
              for (const result of added) {
                if (!existingTrackIds.has(result.track.id)) {
                  tracks.push(result.track)
                  existingTrackIds.add(result.track.id)
                }
                if (!existingAlbumIds.has(result.album.id)) {
                  albums.push(result.album)
                  existingAlbumIds.add(result.album.id)
                }
              }
            }
          } catch (err) {
            console.error('Failed to reconcile library:', err)
          }
        }

        if (albums.length > 0 || tracks.length > 0) {
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

  // Debounced auto-save when albums/tracks/folders change
  useEffect(() => {
    // Don't save until initial load is complete
    if (!hasLoadedRef.current) return
    if (!window.electronAPI?.saveLibrary) return

    // Don't save empty library (prevents wiping data on fresh mount before load)
    if (albums.length === 0 && tracks.length === 0 && folders.length === 0) return

    clearTimeout(saveTimerRef.current!)
    saveTimerRef.current = setTimeout(() => {
      window.electronAPI!.saveLibrary({ albums, tracks, folders }).then((artUpdates) => {
        // When base64 data URIs are externalized to artwork:// URLs on disk,
        // update in-memory albums so caches (e.g. segmentation) use stable keys
        // that will match on the next app launch.
        if (artUpdates && Object.keys(artUpdates).length > 0) {
          setAlbums((prev) =>
            prev.map((a) => {
              const newArt = artUpdates[a.id]
              return newArt ? { ...a, art: newArt } : a
            })
          )
        }
      }).catch((err: unknown) => {
        console.error('Failed to save library:', err)
      })
    }, 500)

    return () => clearTimeout(saveTimerRef.current!)
  }, [albums, tracks, folders])

  const albumMap = useMemo(
    () => new Map(albums.map((a) => [a.id, a])),
    [albums]
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
      setAlbums((prev) => {
        const existingKeys = new Set(prev.map((a) => a.title))
        const newAlbums = result.albums.filter((a) => !existingKeys.has(a.title))
        return [...prev, ...newAlbums]
      })

      setTracks((prev) => {
        const existingIds = new Set(prev.map((t) => t.id))
        const newTracks = result.tracks.filter((t) => !existingIds.has(t.id))
        return [...prev, ...newTracks]
      })

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
  }, [])

  const removeFolder = useCallback((folderPath: string) => {
    // Compute album IDs to keep from the ref (avoids stale closure from
    // nesting setAlbums inside setTracks updater — audit #9)
    const remaining = tracksRef.current.filter((t) => !t.filePath.startsWith(folderPath))
    const albumIdsWithTracks = new Set(remaining.map((t) => t.albumId))
    setTracks(remaining)
    setAlbums((prev) => prev.filter((a) => albumIdsWithTracks.has(a.id)))
    setFolders((prev) => prev.filter((f) => f !== folderPath))
  }, [])

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
          // Merge new track into library
          setTracks((prev) => {
            if (prev.some((t) => t.id === result.track.id)) return prev
            return [...prev, result.track]
          })
          setAlbums((prev) => {
            if (prev.some((a) => a.id === result.album.id)) return prev
            return [...prev, result.album]
          })
        }).catch((err: unknown) => {
          console.error('Failed to scan new file:', err)
        })
      } else if (event.type === 'unlink') {
        const remaining = tracksRef.current.filter((t) => t.filePath !== event.filePath)
        const albumIdsWithTracks = new Set(remaining.map((t) => t.albumId))
        setTracks(remaining)
        setAlbums((prev) => prev.filter((a) => albumIdsWithTracks.has(a.id)))
      }
    })

    return () => {
      unsubscribe()
      window.electronAPI!.stopWatching()
    }
  }, [folders])

  return {
    albums,
    tracks,
    folders,
    isLoading,
    isScanning,
    scanError,
    scanProgress,
    getAlbumForTrack,
    getTracksForAlbum,
    importFolder,
    removeFolder,
  }
}
