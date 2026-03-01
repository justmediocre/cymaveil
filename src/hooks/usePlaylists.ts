import { useState, useCallback, useEffect, useRef } from 'react'
import type { Playlist, Track } from '../types'

const FAVORITES_ID = 'favorites'

function createPlaylistObj(name: string): Playlist {
  return {
    id: `playlist-${Date.now()}`,
    name,
    trackIds: [],
    createdAt: Date.now(),
  }
}

export default function usePlaylists() {
  const [playlists, setPlaylists] = useState<Playlist[]>([])
  const hasLoadedRef = useRef<boolean>(false)
  const saveTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null)

  // Load playlists on mount
  useEffect(() => {
    async function load() {
      if (!window.electronAPI?.loadPlaylists) {
        // No electron — create favorites in-memory
        setPlaylists([{ id: FAVORITES_ID, name: 'Favorites', trackIds: [], createdAt: 0 }])
        hasLoadedRef.current = true
        return
      }

      try {
        let data = await window.electronAPI.loadPlaylists()
        if (!Array.isArray(data)) data = []

        // Ensure favorites playlist exists
        if (!data.find((p) => p.id === FAVORITES_ID)) {
          data = [{ id: FAVORITES_ID, name: 'Favorites', trackIds: [], createdAt: 0 }, ...data]
        }

        setPlaylists(data)
      } catch (err) {
        console.error('Failed to load playlists:', err)
        setPlaylists([{ id: FAVORITES_ID, name: 'Favorites', trackIds: [], createdAt: 0 }])
      } finally {
        hasLoadedRef.current = true
      }
    }
    load()
  }, [])

  // Debounced auto-save
  useEffect(() => {
    if (!hasLoadedRef.current) return
    if (!window.electronAPI?.savePlaylists) return

    clearTimeout(saveTimerRef.current!)
    saveTimerRef.current = setTimeout(() => {
      window.electronAPI!.savePlaylists(playlists).catch((err: unknown) => {
        console.error('Failed to save playlists:', err)
      })
    }, 500)

    return () => clearTimeout(saveTimerRef.current!)
  }, [playlists])

  const createPlaylist = useCallback((name: string): Playlist => {
    const playlist = createPlaylistObj(name)
    setPlaylists((prev) => [...prev, playlist])
    return playlist
  }, [])

  const deletePlaylist = useCallback((id: string) => {
    if (id === FAVORITES_ID) return
    setPlaylists((prev) => prev.filter((p) => p.id !== id))
  }, [])

  const renamePlaylist = useCallback((id: string, name: string) => {
    const trimmed = name.trim()
    if (id === FAVORITES_ID || !trimmed) return
    setPlaylists((prev) => prev.map((p) => (p.id === id ? { ...p, name: trimmed } : p)))
  }, [])

  const addTrackToPlaylist = useCallback((playlistId: string, trackId: string) => {
    setPlaylists((prev) =>
      prev.map((p) => {
        if (p.id !== playlistId) return p
        if (p.trackIds.includes(trackId)) return p
        return { ...p, trackIds: [...p.trackIds, trackId] }
      })
    )
  }, [])

  const removeTrackFromPlaylist = useCallback((playlistId: string, trackId: string) => {
    setPlaylists((prev) =>
      prev.map((p) => {
        if (p.id !== playlistId) return p
        return { ...p, trackIds: p.trackIds.filter((id) => id !== trackId) }
      })
    )
  }, [])

  const reorderPlaylist = useCallback((playlistId: string, fromIndex: number, toIndex: number) => {
    setPlaylists((prev) =>
      prev.map((p) => {
        if (p.id !== playlistId) return p
        const ids = [...p.trackIds]
        const [moved] = ids.splice(fromIndex, 1)
        if (moved !== undefined) ids.splice(toIndex, 0, moved)
        return { ...p, trackIds: ids }
      })
    )
  }, [])

  const isTrackFavorited = useCallback(
    (trackId: string): boolean => {
      const fav = playlists.find((p) => p.id === FAVORITES_ID)
      return fav ? fav.trackIds.includes(trackId) : false
    },
    [playlists]
  )

  const toggleFavorite = useCallback((trackId: string) => {
    setPlaylists((prev) =>
      prev.map((p) => {
        if (p.id !== FAVORITES_ID) return p
        if (p.trackIds.includes(trackId)) {
          return { ...p, trackIds: p.trackIds.filter((id) => id !== trackId) }
        }
        return { ...p, trackIds: [...p.trackIds, trackId] }
      })
    )
  }, [])

  const exportPlaylist = useCallback(async (playlist: Playlist, tracks: Track[]): Promise<string | null> => {
    if (!window.electronAPI?.exportPlaylist) return null
    return await window.electronAPI.exportPlaylist(playlist, tracks)
  }, [])

  const importPlaylist = useCallback(
    async (allTracks: Track[]): Promise<Playlist | null> => {
      if (!window.electronAPI?.importPlaylist) return null
      const result = await window.electronAPI.importPlaylist()
      if (!result) return null

      // Match imported file paths to existing track IDs
      const filePathMap = new Map<string, string>()
      for (const track of allTracks) {
        if (track.filePath) {
          filePathMap.set(track.filePath, track.id)
        }
      }

      const matchedIds: string[] = []
      for (const fp of result.filePaths) {
        const id = filePathMap.get(fp)
        if (id) matchedIds.push(id)
      }

      const playlist = createPlaylistObj(result.name)
      playlist.trackIds = matchedIds
      setPlaylists((prev) => [...prev, playlist])
      return playlist
    },
    []
  )

  return {
    playlists,
    createPlaylist,
    deletePlaylist,
    renamePlaylist,
    addTrackToPlaylist,
    removeTrackFromPlaylist,
    reorderPlaylist,
    isTrackFavorited,
    toggleFavorite,
    exportPlaylist,
    importPlaylist,
  }
}
