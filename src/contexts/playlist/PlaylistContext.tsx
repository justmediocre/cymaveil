import { createContext, useContext, useMemo } from 'react'
import usePlaylists from '../../hooks/usePlaylists'

type PlaylistContextValue = ReturnType<typeof usePlaylists>

const PlaylistContext = createContext<PlaylistContextValue | null>(null)

export function PlaylistProvider({ children }: { children: React.ReactNode }) {
  const {
    playlists, createPlaylist, deletePlaylist, renamePlaylist,
    addTrackToPlaylist, removeTrackFromPlaylist, reorderPlaylist,
    isTrackFavorited, toggleFavorite, exportPlaylist, importPlaylist,
  } = usePlaylists()

  const value = useMemo<PlaylistContextValue>(
    () => ({
      playlists, createPlaylist, deletePlaylist, renamePlaylist,
      addTrackToPlaylist, removeTrackFromPlaylist, reorderPlaylist,
      isTrackFavorited, toggleFavorite, exportPlaylist, importPlaylist,
    }),
    [
      playlists, createPlaylist, deletePlaylist, renamePlaylist,
      addTrackToPlaylist, removeTrackFromPlaylist, reorderPlaylist,
      isTrackFavorited, toggleFavorite, exportPlaylist, importPlaylist,
    ],
  )

  return <PlaylistContext.Provider value={value}>{children}</PlaylistContext.Provider>
}

export function usePlaylistCtx(): PlaylistContextValue {
  const ctx = useContext(PlaylistContext)
  if (!ctx) throw new Error('usePlaylistCtx must be inside PlaylistProvider')
  return ctx
}
