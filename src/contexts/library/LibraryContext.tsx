import { createContext, useContext, useMemo } from 'react'
import useLibrary from '../../hooks/useLibrary'

type LibraryContextValue = ReturnType<typeof useLibrary>

const LibraryContext = createContext<LibraryContextValue | null>(null)

export function LibraryProvider({ children }: { children: React.ReactNode }) {
  const {
    albums, tracks, folders, isLoading, isScanning, scanError, scanProgress,
    getAlbumForTrack, getTracksForAlbum, importFolder, removeFolder,
  } = useLibrary()

  const value = useMemo<LibraryContextValue>(
    () => ({
      albums, tracks, folders, isLoading, isScanning, scanError, scanProgress,
      getAlbumForTrack, getTracksForAlbum, importFolder, removeFolder,
    }),
    [
      albums, tracks, folders, isLoading, isScanning, scanError, scanProgress,
      getAlbumForTrack, getTracksForAlbum, importFolder, removeFolder,
    ],
  )

  return <LibraryContext.Provider value={value}>{children}</LibraryContext.Provider>
}

export function useLibraryCtx(): LibraryContextValue {
  const ctx = useContext(LibraryContext)
  if (!ctx) throw new Error('useLibraryCtx must be inside LibraryProvider')
  return ctx
}
