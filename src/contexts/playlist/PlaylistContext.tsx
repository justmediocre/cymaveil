import { createContext, useContext } from 'react'
import usePlaylists from '../../hooks/usePlaylists'

type PlaylistContextValue = ReturnType<typeof usePlaylists>

const PlaylistContext = createContext<PlaylistContextValue | null>(null)

export function PlaylistProvider({ children }: { children: React.ReactNode }) {
  const playlists = usePlaylists()
  return <PlaylistContext.Provider value={playlists}>{children}</PlaylistContext.Provider>
}

export function usePlaylistCtx(): PlaylistContextValue {
  const ctx = useContext(PlaylistContext)
  if (!ctx) throw new Error('usePlaylistCtx must be inside PlaylistProvider')
  return ctx
}
