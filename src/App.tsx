import { LibraryProvider, useLibraryCtx } from './contexts/library/LibraryContext'
import { PlaylistProvider } from './contexts/playlist/PlaylistContext'
import { PlaybackProvider } from './contexts/playback/PlaybackContext'
import { ThemeProvider } from './contexts/ThemeContext'
import AppLayout from './components/layout/AppLayout'

const LOADING_SPINNER = (
  <div className="flex h-screen items-center justify-center" style={{ background: '#0a0a0b' }}>
    <div className="text-sm" style={{ color: 'rgba(255,255,255,0.3)' }}>Loading library...</div>
  </div>
)

function LibraryGate({ children }: { children: React.ReactNode }) {
  const { isLoading } = useLibraryCtx()
  if (isLoading) return LOADING_SPINNER
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
