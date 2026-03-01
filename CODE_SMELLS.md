# Code Smells

## High-Priority (Medium Severity)

- [x] **1. Context values not memoized — causes unnecessary re-renders**
  - `src/contexts/library/LibraryContext.tsx` and `src/contexts/playlist/PlaylistContext.tsx` pass a new object literal as the context value every render. All consumers re-render even when nothing they use changed. Compare to `ThemeContext.tsx` and `PlaybackContext.tsx` which correctly use `useMemo`.

- [x] **2. Missing `useCallback` on handlers passed to `TrackList` — breaks `TrackRow` memoization**
  - `src/components/views/PlaylistDetailView.tsx:44-76` — 6 handlers defined as plain functions, not `useCallback`. Passed to `TrackList` which has `memo`-wrapped `TrackRow` children, defeating the memoization.
  - `src/components/layout/QueuePanel.tsx:28` — `handleQueueTrackSelect` is also not wrapped in `useCallback`, same issue.

- [ ] **3. Stale store read at render time**
  - `src/contexts/playback/PlaybackContext.tsx:183-184` — `crossfadeDuration` is read from `playbackSettingsStore.get()` during render without a subscription. If the user changes crossfade in settings, `aboutToEndThreshold` stays stale until an unrelated re-render. Should use `useSyncExternalStore` like `usePlaybackSettings` does.

- [ ] **4. Duplicated visualizer logic across 4 files**
  - `src/lib/visualizers/{fullSurface,mirroredBars,contourBars,radialBurst}.ts` all copy the same smoothing expression (`smoothed[i]! * 0.4 + rawValue * 0.6`), FFT truncation (`dataArray.length * 0.93`), and log-scale mapping (`Math.pow(t, 1.5)`). These belong in `barHelpers.ts`.

- [ ] **5. `setAlbums` called inside `setTracks` updater — React anti-pattern**
  - `src/hooks/useLibrary.ts:177-183` — setState inside another setState updater is impure and breaks concurrent mode guarantees. The `removeFolder` callback on line 140 shows the correct pattern.

- [ ] **6. Missing IPC input validation (security)**
  - `electron/main.js:241` — `playlists:save` passes `data` straight to `savePlaylists` with no shape validation (compare to `library:save` which validates).
  - `electron/main.js:258` — `maskOverrides:export` writes `jsonData` to disk without a `typeof` check.

- [ ] **7. Expensive computation without `useMemo`**
  - `src/components/views/AlbumDetailView.tsx:21-22` — `albums.find()` (O(n)) and `getTracksForAlbum()` (O(n log n) filter+sort) run every render without memoization. These re-execute on every playback tick.

- [ ] **8. Inconsistent async error handling strategy**
  - `useLibrary.ts` and `usePlaylists.ts`: DEV-only logging (`import.meta.env.DEV`)
  - `useUpdateChecker.ts:24`: no error handling at all
  - `electron/libraryStore.js`: always logs
  - `electron/updateChecker.js`: silently swallows (`catch { return null }`)
  - Production builds lose all visibility into library load/save failures.

- [ ] **9. `setTimeout` loop instead of `requestAnimationFrame` for animation**
  - `src/components/Visualizer.tsx:146` and `src/lib/tickLoop.ts` — Use `setTimeout(render, 33)` for ~30fps. This doesn't sync with display refresh rate, causing micro-jitter especially on 120Hz displays.

## Lower-Priority (Low Severity)

- [ ] **10. Dead code**
  - `src/components/AlbumArt.tsx:4` — `perfMarkStart` imported but never used.
  - `src/lib/visualizers/radialBurst.ts:6,15` — Module-level `smoothed` variable allocated in `init()` but `render()` uses `hostSmoothed` parameter instead.
  - `src/components/VisualizerBackground.tsx:8-9` — `dominantColor` and `accentColor` declared in props interface and passed by caller but never read.
  - `src/contexts/playback/PlaybackContext.tsx:507-514` — `currentAlbumWithColors` spreads `currentAlbum` then re-assigns `dominantColor` and `accentColor` with their own values — a no-op.

- [ ] **11. `any` types in segmentation worker**
  - `src/lib/segmentation/segmentation.worker.ts` — `pipeline: any` (line 28), `dtype as any` (line 57), `result: any` (line 69). The `dtype` cast is avoidable since `DepthModelDtype` already exists in `types.ts`.

- [ ] **12. Magic numbers**
  - `0.93` FFT truncation — 4 files, no constant or comment explaining the 7% high-frequency cutoff.
  - `2000` ms status reset — repeated 7 times in `DepthLayersTab.tsx`.
  - `0.6` bass threshold and `60` ms debounce — `AlbumArt.tsx:198-199`.
  - Shadow/glow/core line widths `6/3/1.5` — duplicated in `waveform.ts` and `radialBurst.ts`.

- [ ] **13. Duplicated loading spinner**
  - `src/App.tsx:7` and `src/components/layout/AppLayout.tsx:18-22` define identical `LOADING_SPINNER` constants.

- [ ] **14. Duplicate luminance formula**
  - `0.299 * r + 0.587 * g + 0.114 * b` hardcoded in both `edgeDetector.ts:37` and `depthToMask.ts:343`.

- [ ] **15. Ambiguous file naming**
  - `src/components/Controls.tsx` (playback transport) vs `src/components/settings/Controls.tsx` (settings UI primitives) — same filename, different purposes.

- [ ] **16. `clearLibrary` aborts on first file deletion failure**
  - `electron/libraryStore.js:199-214` — If one `unlinkSync` fails, the catch aborts the loop, leaving orphaned artwork files. A per-file try/catch would be more robust.

- [ ] **17. `VolumeControl.tsx:79` — `useCallback` recreated on every volume change**
  - `volumeFromY` captures `volume` in its closure, causing re-creation on every volume update. Store `volume` in a ref instead.

- [ ] **18. Mixed hover interaction styles**
  - `LibraryTab.tsx` and `AddToPlaylistMenu.tsx` use imperative `e.currentTarget.style` mutation for hover effects, while the rest of the codebase uses Framer Motion's `whileHover`.
