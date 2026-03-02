import { useState, useCallback, useEffect } from 'react'
import { cmdOrCtrl } from '../../lib/keyboard'
import { usePlayback } from '../../contexts/playback/PlaybackContext'
import { useLibraryCtx } from '../../contexts/library/LibraryContext'
import Sidebar from '../Sidebar'
import TitleBar from './TitleBar'
import QueuePanel from './QueuePanel'
import MiniPlayer from '../MiniPlayer'
import NowPlayingView from '../views/NowPlayingView'
import BrowseRouter from '../views/BrowseRouter'
import AlbumArtBackground from '../AlbumArtBackground'
import PerfHUD from '../PerfHUD'
import UpdateNotification from '../UpdateNotification'
import ErrorBoundary from '../ErrorBoundary'
import LoadingSpinner from '../LoadingSpinner'
import useBatchSegmentation from '../../hooks/useBatchSegmentation'
import useUpdateChecker from '../../hooks/useUpdateChecker'
import { perfCountRender } from '../../lib/perf'

export default function AppLayout() {
  perfCountRender('App')

  const { albums, tracks } = useLibraryCtx()
  const { state, currentTrack, currentAlbum, upNextTracks, isPlaying, handlePlayPause, handleNext, handlePrev } = usePlayback()
  const batchSeg = useBatchSegmentation(albums)
  const { updateInfo, dismiss: dismissUpdate, openRelease } = useUpdateChecker()

  const [sidebarOpen, setSidebarOpen] = useState(true)
  const [showQueue, setShowQueue] = useState(false)
  const [activeNav, setActiveNav] = useState(() => tracks.length === 0 ? 'Albums' : 'NowPlaying')
  const [previousNav, setPreviousNav] = useState('Library')
  const [selectedAlbumId, setSelectedAlbumId] = useState<string | null>(null)
  const [selectedPlaylistId, setSelectedPlaylistId] = useState<string | null>(null)
  const [isFullscreen, setIsFullscreen] = useState(false)

  // Sidebar (260px) + Queue (320px) + usable content (~500px) = 1080px
  const NARROW_THRESHOLD = 1080

  // When window is too narrow for both panels, make toggles mutually exclusive
  const handleToggleSidebar = useCallback(() => {
    const willOpen = !sidebarOpen
    if (willOpen && showQueue && window.innerWidth < NARROW_THRESHOLD) {
      setShowQueue(false)
    }
    setSidebarOpen(willOpen)
  }, [sidebarOpen, showQueue])

  const handleToggleQueue = useCallback(() => {
    const willOpen = !showQueue
    if (willOpen && sidebarOpen && window.innerWidth < NARROW_THRESHOLD) {
      setSidebarOpen(false)
    }
    setShowQueue(willOpen)
  }, [showQueue, sidebarOpen])

  // Auto-collapse sidebar if both panels are open and window resizes below threshold
  useEffect(() => {
    const onResize = () => {
      if (window.innerWidth < NARROW_THRESHOLD && sidebarOpen && showQueue) {
        setSidebarOpen(false)
      }
    }
    window.addEventListener('resize', onResize)
    return () => window.removeEventListener('resize', onResize)
  }, [sidebarOpen, showQueue])

  // Global keyboard shortcuts
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'F11') {
        e.preventDefault()
        window.electronAPI?.windowToggleFullscreen()
      }
      if (e.key === 'Escape' && isFullscreen) {
        window.electronAPI?.windowToggleFullscreen()
      }
      // Tab → toggle queue panel
      if (e.key === 'Tab' && !e.ctrlKey && !e.altKey && !e.metaKey) {
        const el = e.target as HTMLElement
        if (
          el?.closest?.(
            'input, textarea, select, [contenteditable="true"]',
          )
        )
          return
        e.preventDefault()
        handleToggleQueue()
      }
      // Ctrl+Right / Cmd+Right → next track
      if (e.key === 'ArrowRight' && cmdOrCtrl(e) && !e.altKey) {
        e.preventDefault()
        handleNext()
      }
      // Ctrl+Left / Cmd+Left → previous track
      if (e.key === 'ArrowLeft' && cmdOrCtrl(e) && !e.altKey) {
        e.preventDefault()
        handlePrev()
      }
      // Spacebar → play/pause (skip when focused on interactive UI elements)
      if (e.code === 'Space' && !e.ctrlKey && !e.altKey && !e.metaKey) {
        const el = e.target as HTMLElement
        if (
          el?.closest?.(
            'input, textarea, select, button, a, summary, [contenteditable="true"], [role="button"], [role="checkbox"], [role="switch"], [role="slider"], [role="tab"], [role="menuitem"], [role="option"]',
          )
        )
          return
        e.preventDefault()
        handlePlayPause()
      }
    }
    window.addEventListener('keydown', handleKeyDown)
    return () => window.removeEventListener('keydown', handleKeyDown)
  }, [handlePlayPause, handleNext, handlePrev, handleToggleQueue, isFullscreen])

  // Listen for fullscreen state changes from Electron
  useEffect(() => {
    if (!window.electronAPI?.onFullscreenChange) return
    return window.electronAPI.onFullscreenChange(setIsFullscreen)
  }, [])

  // Auto-hide cursor after idle in immersive fullscreen
  const isImmersive = isFullscreen && activeNav === 'NowPlaying' && state.isReady && !!currentTrack && !!currentAlbum
  useEffect(() => {
    if (!isImmersive) return
    let timer: ReturnType<typeof setTimeout>
    const hide = () => { document.documentElement.style.cursor = 'none' }
    const show = () => {
      document.documentElement.style.cursor = ''
      clearTimeout(timer)
      timer = setTimeout(hide, 2500)
    }
    timer = setTimeout(hide, 2500)
    window.addEventListener('mousemove', show)
    return () => {
      clearTimeout(timer)
      document.documentElement.style.cursor = ''
      window.removeEventListener('mousemove', show)
    }
  }, [isImmersive])

  // Navigate to album detail
  const handleAlbumSelect = useCallback((albumId: string) => {
    setSelectedAlbumId(albumId)
    setActiveNav('AlbumDetail')
  }, [])

  // Navigate to playlist detail
  const handlePlaylistSelect = useCallback((playlistId: string) => {
    setSelectedPlaylistId(playlistId)
    setActiveNav('PlaylistDetail')
  }, [])

  // Expand to full player (save current nav)
  const handleExpandPlayer = useCallback(() => {
    setPreviousNav(activeNav)
    setActiveNav('NowPlaying')
  }, [activeNav])

  // Sidebar navigation — saves previous nav when switching to NowPlaying
  const handleNavChange = useCallback((nav: string) => {
    if (nav === 'NowPlaying') {
      if (activeNav !== 'NowPlaying') {
        setPreviousNav(activeNav)
      }
      setActiveNav('NowPlaying')
    } else {
      setActiveNav(nav)
    }
  }, [activeNav])

  // Collapse full player (restore previous nav)
  const handleCollapsePlayer = useCallback(() => {
    setActiveNav(previousNav)
  }, [previousNav])

  // Navigate to Now Playing (used by shuffleAll in BrowseRouter)
  const handleNavigateToNowPlaying = useCallback(() => {
    setPreviousNav(activeNav)
    setActiveNav('NowPlaying')
  }, [activeNav])

  // Keep the loading state visible until playback state finishes restoring.
  // Only gate when there are tracks (empty library should show the app immediately).
  if (tracks.length > 0 && !state.isReady) return <LoadingSpinner />

  const isNowPlaying = activeNav === 'NowPlaying'
  const hasTrack = state.isReady && currentTrack && currentAlbum
  const immersive = isFullscreen && isNowPlaying && !!hasTrack

  return (
    <div className="h-screen overflow-hidden relative">
      {/* Album art mosaic background */}
      <AlbumArtBackground albums={albums} isPlaying={isPlaying} />

      {/* App UI layer */}
      <div className="flex h-full relative" style={{ zIndex: 10 }}>
        {/* Sidebar — hidden in immersive fullscreen */}
        {!immersive && (
          <Sidebar
            isOpen={sidebarOpen}
            activeNav={activeNav}
            onNavChange={handleNavChange}
            upNextTracks={upNextTracks}
          />
        )}

        {/* Main content */}
        <div
          className={`flex-1 flex flex-col min-w-0 relative ${isNowPlaying && hasTrack ? '' : 'glass'}`}
          style={{ background: isNowPlaying && hasTrack ? 'transparent' : 'var(--glass-bg)' }}
        >
          {/* Title bar — hidden in immersive fullscreen */}
          {!immersive && (
            <TitleBar
              sidebarOpen={sidebarOpen}
              onToggleSidebar={handleToggleSidebar}
              showQueue={showQueue}
              onToggleQueue={handleToggleQueue}
              isFullscreen={isFullscreen}
              onToggleFullscreen={() => window.electronAPI?.windowToggleFullscreen()}
              batchProgress={batchSeg}
            />
          )}

          {/* Content area */}
          <div className="flex-1 flex overflow-hidden">
            {/* NowPlaying — stays mounted once a track exists so album art,
                segmentation, contour data, and colors remain pre-warmed.
                Hidden via CSS when browsing to avoid layout interference. */}
            {hasTrack && (
              <div style={isNowPlaying ? { display: 'flex', flex: 1 } : { display: 'none' }}>
                <ErrorBoundary
                  onReset={handleCollapsePlayer}
                  fallback={(error, reset) => (
                    <div style={{
                      display: 'flex', flex: 1, flexDirection: 'column', alignItems: 'center', justifyContent: 'center',
                      color: 'var(--text-secondary)', fontFamily: 'var(--font-sans, system-ui, sans-serif)',
                    }}>
                      <p style={{ fontSize: '0.875rem', marginBottom: '1rem' }}>Player failed to render</p>
                      <button
                        onClick={reset}
                        style={{
                          padding: '0.5rem 1rem', borderRadius: '0.5rem', border: 'none', cursor: 'pointer',
                          background: 'rgba(255,255,255,0.1)', color: 'var(--text-primary)', fontSize: '0.8125rem',
                        }}
                      >
                        Go Back
                      </button>
                      {import.meta.env.DEV && (
                        <details style={{ marginTop: '1rem', maxWidth: '32rem', fontSize: '0.75rem', opacity: 0.6 }}>
                          <summary style={{ cursor: 'pointer' }}>Error details</summary>
                          <pre style={{ whiteSpace: 'pre-wrap', marginTop: '0.5rem' }}>{error.message}</pre>
                        </details>
                      )}
                    </div>
                  )}
                >
                  <NowPlayingView
                    onCollapse={handleCollapsePlayer}
                    immersive={immersive}
                    isVisible={isNowPlaying}
                  />
                </ErrorBoundary>
              </div>
            )}

            {/* Browse view + mini player */}
            {!(isNowPlaying && hasTrack) && (
              <div className="flex-1 flex flex-col min-w-0">
                <div className="flex-1 min-h-0">
                  <BrowseRouter
                    activeNav={activeNav}
                    selectedAlbumId={selectedAlbumId}
                    selectedPlaylistId={selectedPlaylistId}
                    onAlbumSelect={handleAlbumSelect}
                    onPlaylistSelect={handlePlaylistSelect}
                    onBackToAlbums={() => setActiveNav('Albums')}
                    onBackToPlaylists={() => setActiveNav('Playlists')}
                    onBackToLibrary={() => setActiveNav('Library')}
                    onNavigateToNowPlaying={handleNavigateToNowPlaying}
                    onSetActiveNav={setActiveNav}
                    onProcessAll={batchSeg.processAll}
                    batchProcessing={batchSeg.processing}
                  />
                </div>
                {hasTrack && (
                  <MiniPlayer onExpand={handleExpandPlayer} />
                )}
              </div>
            )}

            {/* Queue panel — hidden in immersive fullscreen */}
            {!immersive && hasTrack && (
              <QueuePanel show={showQueue} />
            )}
          </div>
        </div>
      </div>
      <UpdateNotification info={updateInfo} onDismiss={dismissUpdate} onOpenRelease={openRelease} />
      <PerfHUD />
    </div>
  )
}
