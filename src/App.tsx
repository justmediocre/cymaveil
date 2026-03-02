import { LibraryProvider, useLibraryCtx } from './contexts/library/LibraryContext'
import { PlaylistProvider } from './contexts/playlist/PlaylistContext'
import { PlaybackProvider } from './contexts/playback/PlaybackContext'
import { ThemeProvider } from './contexts/ThemeContext'
import AppLayout from './components/layout/AppLayout'
import LoadingSpinner from './components/LoadingSpinner'

function LibraryGate({ children }: { children: React.ReactNode }) {
  const { isLoading } = useLibraryCtx()
  if (isLoading) return <LoadingSpinner />
  return <>{children}</>
}

export default function App() {
  return (
    <ThemeProvider>
      <LibraryProvider>
        <PlaylistProvider>
          <LibraryGate>
            <PlaybackProvider>
              <AppLayout />
            </PlaybackProvider>
          </LibraryGate>
        </PlaylistProvider>
      </LibraryProvider>
    </ThemeProvider>
  )
}
