import { useState, useMemo, useCallback, useRef, memo } from 'react'
import { motion, AnimatePresence } from 'motion/react'
import { usePlayback } from '../../contexts/playback/PlaybackContext'
import { usePlaylistCtx } from '../../contexts/playlist/PlaylistContext'
import useAlbumColors from '../../hooks/useAlbumColors'
import useContourPath from '../../hooks/useContourPath'
import useSegmentation from '../../hooks/useSegmentation'
import useVisualSettings from '../../hooks/useVisualSettings'
import useMaskEditor from '../../hooks/useMaskEditor'
import useMaskBrushEditor from '../../hooks/useMaskBrushEditor'
import { CONCRETE_STYLES } from '../../lib/visualizers'
import AlbumArt from '../AlbumArt'
import ErrorBoundary from '../ErrorBoundary'
import MaskEditor from '../MaskEditor'
import MaskBrushEditor from '../MaskBrushEditor'
import Visualizer from '../Visualizer'
import VisualizerBackground from '../VisualizerBackground'
import NowPlaying from '../NowPlaying'
import Controls from '../Controls'
import ProgressBar from '../ProgressBar'
import VolumeControl from '../VolumeControl'

interface NowPlayingViewProps {
  onCollapse: () => void
  immersive: boolean
  isVisible: boolean
}

export default memo(function NowPlayingView({ onCollapse, immersive, isVisible }: NowPlayingViewProps) {
  const {
    state, currentTrack, currentAlbum, isPlaying, duration, analyserRef, dataArrayRef,
    bassEnergyRef, transitionIntent, clearTransitionIntent, handleNext, handlePrev,
    handlePlayPause, handleShuffleToggle, handleRepeatToggle, setVolume, seek,
  } = usePlayback()
  const { isTrackFavorited, toggleFavorite, addTrackToPlaylist, createPlaylist, playlists } = usePlaylistCtx()

  // Track which album AlbumArt is actually displaying (lags behind currentAlbum
  // during vinyl/art transitions) so colors stay in sync with the visible art.
  const [displayedAlbum, setDisplayedAlbum] = useState(currentAlbum)
  const handleDisplayedAlbumChange = useCallback((a: typeof displayedAlbum) => setDisplayedAlbum(a), [])

  // Visual hooks — only mounted when NowPlaying is visible
  const albumColors = useAlbumColors(displayedAlbum)
  const currentAlbumWithColors = useMemo(() => {
    if (!currentAlbum) return null
    return {
      ...currentAlbum,
      dominantColor: albumColors.dominant,
      accentColor: albumColors.accent,
    }
  }, [currentAlbum, albumColors.dominant, albumColors.accent])

  const { contourData } = useContourPath(currentAlbumWithColors?.art ?? null)
  const {
    segmentation: baseSegmentation,
    loading: segmentationLoading,
    depthMap,
    effectivePostProcessParams,
    effectiveModelParams,
    hasOverride,
    refresh: refreshSegmentation,
  } = useSegmentation(currentAlbumWithColors?.art ?? null)
  const maskEditor = useMaskEditor(currentAlbumWithColors?.art ?? null)
  const brushEditor = useMaskBrushEditor()
  const segmentation = maskEditor.previewSegmentation ?? baseSegmentation

  // Wrap save/remove to refresh base segmentation after the override changes
  const handleSave = useCallback(async (
    artSrc: string,
    backendId: Parameters<typeof maskEditor.save>[1],
    modelParams: Parameters<typeof maskEditor.save>[2],
    postProcessParams: Parameters<typeof maskEditor.save>[3],
  ) => {
    await maskEditor.save(artSrc, backendId, modelParams, postProcessParams)
    refreshSegmentation()
  }, [maskEditor.save, refreshSegmentation])

  const handleRemoveOverride = useCallback(async (
    artSrc: string,
    backendId: Parameters<typeof maskEditor.removeOverride>[1],
    defaultPostParams: Parameters<typeof maskEditor.removeOverride>[2],
  ) => {
    await maskEditor.removeOverride(artSrc, backendId, defaultPostParams)
    refreshSegmentation()
  }, [maskEditor.removeOverride, refreshSegmentation])
  const handleOpenBrushEditor = useCallback(() => {
    const art = currentAlbumWithColors?.art
    if (!art) return
    brushEditor.open(baseSegmentation, art)
  }, [brushEditor, baseSegmentation, currentAlbumWithColors?.art])

  const handleBrushSave = useCallback(() => {
    refreshSegmentation()
  }, [refreshSegmentation])

  const { settings: visualSettings } = useVisualSettings()

  const activelyPlaying = isPlaying && isVisible

  // Resolve 'random' visualizer style here (NowPlayingView never unmounts)
  // rather than inside Visualizer (which unmounts/remounts via AnimatePresence
  // during album art transitions), preventing double random re-picks.
  // Keyed on displayedAlbum.id so the style change syncs with the visual
  // album art transition, not the earlier state change from dispatch(NEXT).
  const randomPickRef = useRef<Exclude<typeof visualSettings.visualizerStyle, 'random'> | null>(null)
  const lastRandomAlbumRef = useRef<string | undefined>(undefined)
  const displayedAlbumId = displayedAlbum?.id

  if (visualSettings.visualizerStyle === 'random') {
    if (randomPickRef.current === null || displayedAlbumId !== lastRandomAlbumRef.current) {
      randomPickRef.current = CONCRETE_STYLES[Math.floor(Math.random() * CONCRETE_STYLES.length)]!
      lastRandomAlbumRef.current = displayedAlbumId
    }
  } else {
    randomPickRef.current = null
    lastRandomAlbumRef.current = undefined
  }

  const resolvedVisualizerStyle = visualSettings.visualizerStyle === 'random'
    ? randomPickRef.current!
    : visualSettings.visualizerStyle

  const visualizerElement = useMemo(
    () => (
      <Visualizer
        contourData={contourData}
        analyserRef={analyserRef}
        dataArrayRef={dataArrayRef}
        accentColor={currentAlbumWithColors?.accentColor ?? ''}
        isPlaying={activelyPlaying}
        segmentation={segmentation}
        resolvedStyle={resolvedVisualizerStyle}
      />
    ),
    [contourData, analyserRef, dataArrayRef, currentAlbumWithColors?.accentColor, activelyPlaying, segmentation, resolvedVisualizerStyle]
  )

  if (!currentTrack || !currentAlbumWithColors) return null

  return (
    <motion.div
      className={`flex-1 flex flex-col items-center justify-center gap-6 px-6 pb-6 relative ${immersive ? 'pt-6' : ''}`}
    >
      {/* Bass-reactive background glow */}
      <VisualizerBackground
        analyserRef={analyserRef}
        isPlaying={activelyPlaying}
        bassEnergyRef={bassEnergyRef}
      />

      {/* Collapse button — hidden in immersive fullscreen */}
      {!immersive && (
        <motion.button
          onClick={onCollapse}
          className="no-drag absolute top-14 left-4 flex items-center gap-1 text-sm z-10"
          aria-label="Back"
          style={{ color: 'var(--text-secondary)' }}
          whileHover={{ color: 'var(--text-primary)', x: -2 }}
          whileTap={{ scale: 0.97 }}
        >
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
            <polyline points="15 18 9 12 15 6" />
          </svg>
          <span>Back</span>
        </motion.button>
      )}

      {/* Album Art — the centerpiece */}
      <div className="flex-1 flex items-center justify-center max-h-[55%] w-full">
        <AlbumArt
          album={currentAlbumWithColors}
          isPlaying={activelyPlaying}
          trackIndex={state.currentTrackIndex}
          bassEnergyRef={bassEnergyRef}
          segmentation={segmentation}
          segmentationLoading={segmentationLoading}
          transitionIntent={transitionIntent}
          onTransitionDone={clearTransitionIntent}
          onDisplayedAlbumChange={handleDisplayedAlbumChange}
          onEditMask={maskEditor.toggle}
          onEditBrush={handleOpenBrushEditor}
          hasOverride={hasOverride}
          depthLayerActive={visualSettings.depthLayerEnabled}
        >
          {visualizerElement}
        </AlbumArt>
      </div>

      {/* Mask editor — positioned absolutely so it floats above controls */}
      <AnimatePresence>
        {maskEditor.isOpen && currentAlbumWithColors.art && (
          <ErrorBoundary
            onReset={maskEditor.close}
            fallback={(error, reset) => (
              <motion.div
                initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: 10 }}
                className="glass"
                style={{
                  position: 'absolute', bottom: '7rem', left: '50%', transform: 'translateX(-50%)',
                  padding: '1.5rem 2rem', borderRadius: '1rem', zIndex: 50, textAlign: 'center',
                  background: 'var(--glass-bg-surface)', color: 'var(--text-secondary)',
                  fontFamily: 'var(--font-sans, system-ui, sans-serif)', fontSize: '0.8125rem',
                }}
              >
                <p style={{ marginBottom: '0.75rem' }}>Mask editor crashed</p>
                <button
                  onClick={reset}
                  style={{
                    padding: '0.375rem 0.875rem', borderRadius: '0.375rem', border: 'none', cursor: 'pointer',
                    background: 'rgba(255,255,255,0.1)', color: 'var(--text-primary)', fontSize: '0.8125rem',
                  }}
                >
                  Close
                </button>
                {import.meta.env.DEV && (
                  <pre style={{ marginTop: '0.5rem', fontSize: '0.6875rem', opacity: 0.5, whiteSpace: 'pre-wrap' }}>
                    {error.message}
                  </pre>
                )}
              </motion.div>
            )}
          >
            <MaskEditor
              depthMap={depthMap}
              artSrc={currentAlbumWithColors.art}
              width={256}
              height={256}
              initialPostProcessParams={effectivePostProcessParams}
              initialModelParams={effectiveModelParams}
              hasOverride={hasOverride}
              reprocessing={maskEditor.reprocessing}
              downloadProgress={maskEditor.downloadProgress}
              onPreview={maskEditor.previewFromParams}
              onReprocess={maskEditor.reprocess}
              onSave={handleSave}
              onRemoveOverride={handleRemoveOverride}
              onClose={maskEditor.close}
              backendId={visualSettings.segmentationBackend}
              onEditBrush={() => {
                maskEditor.close()
                handleOpenBrushEditor()
              }}
            />
          </ErrorBoundary>
        )}
      </AnimatePresence>

      {/* Brush mask editor — fullscreen overlay */}
      <AnimatePresence>
        {brushEditor.isOpen && (
          <MaskBrushEditor
            editor={brushEditor}
            backendId={visualSettings.segmentationBackend}
            onSave={handleBrushSave}
          >
            {visualizerElement}
          </MaskBrushEditor>
        )}
      </AnimatePresence>

      {/* Controls glass panel */}
      <div
        className="glass w-full max-w-lg mx-auto rounded-2xl px-6 py-5 flex flex-col items-center gap-4"
        style={{ background: 'var(--glass-bg-surface)', zIndex: 2 }}
      >
        {/* Now Playing info */}
        <NowPlaying
          track={currentTrack}
          album={currentAlbumWithColors}
          isTrackFavorited={isTrackFavorited}
          onToggleFavorite={toggleFavorite}
          onAddToPlaylist={addTrackToPlaylist}
          onCreatePlaylist={createPlaylist}
          playlists={playlists}
        />

        {/* Progress Bar */}
        <ProgressBar
          duration={duration || currentTrack.duration}
          onSeek={seek}
        />

        {/* Controls */}
        <div className="relative w-full max-w-md mx-auto px-4">
          <div className="absolute left-4 top-1/2 -translate-y-1/2 z-10">
            <VolumeControl volume={state.volume} onVolumeChange={setVolume} />
          </div>
          <div className="flex justify-center">
            <Controls
              isPlaying={isPlaying}
              onPlayPause={handlePlayPause}
              onNext={handleNext}
              onPrev={handlePrev}
              shuffle={state.shuffle}
              repeat={state.repeat}
              onShuffleToggle={handleShuffleToggle}
              onRepeatToggle={handleRepeatToggle}
            />
          </div>
        </div>
      </div>
    </motion.div>
  )
})
