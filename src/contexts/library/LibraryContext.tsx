import { createContext, useContext } from 'react'
import useLibrary from '../../hooks/useLibrary'

type LibraryContextValue = ReturnType<typeof useLibrary>

const LibraryContext = createContext<LibraryContextValue | null>(null)

export function LibraryProvider({ children }: { children: React.ReactNode }) {
  const library = useLibrary()
  return <LibraryContext.Provider value={library}>{children}</LibraryContext.Provider>
}

export function useLibraryCtx(): LibraryContextValue {
  const ctx = useContext(LibraryContext)
  if (!ctx) throw new Error('useLibraryCtx must be inside LibraryProvider')
  return ctx
}
