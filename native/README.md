# Cymaveil — native rewrite (raylib + C++)

A ground-up reimplementation of Cymaveil in C++20 with [raylib](https://www.raylib.com/),
motivated by resource usage: the Electron app renders through a browser compositor and has
no real idle state. This version draws everything itself and **only spends CPU/GPU when
something is actually happening**:

| State | Behavior |
| --- | --- |
| Playing, window focused | 60 fps (live visualizer) |
| Playing, window unfocused | 24 fps (just keeps the stream fed) |
| Paused, recent input / scanning | 60 fps |
| Idle | Blocks on OS events — near-zero CPU until you touch it |

Press **F3** for a debug overlay showing the current pacing mode.

## Building

The intended workflow is the dev container in `.devcontainer/` at the repo root (CMake +
Ninja on Ubuntu 24.04 with all X11/Wayland/ALSA dev packages). Open the repo in VS Code and
"Reopen in Container", or build with plain CMake on any Linux box with the equivalent
packages:

```bash
cmake -S native -B native/build -G Ninja
cmake --build native/build
./native/build/cymaveil/cymaveil [music-folder ...]
```

Dependencies (raylib, TagLib, nlohmann/json) are pulled via CMake `FetchContent` on first
configure — no system installs needed beyond the windowing/audio dev packages.

Audio inside the dev container is forwarded to the host's PulseAudio/PipeWire socket
(`$XDG_RUNTIME_DIR/pulse` is mounted; miniaudio picks it up via `PULSE_SERVER`).

## Usage

- **Drag & drop** a music folder onto the window to add it to the library (or pass folders
  as CLI args). Rescans happen on a background thread.
- Library / Albums / Now Playing views via the sidebar or keys **1 / 2 / 3**.
- **Space** play/pause · **←/→** seek ±5s · **Ctrl+←/→** prev/next · **↑/↓** volume ·
  **S** shuffle · **R** repeat cycle · **B** animate a mosaic tile · **Esc** back ·
  **F3** debug overlay.

Library cache, settings, and extracted album art live in `~/.local/share/cymaveil/`.

## Status

Done in this first slice:

- [x] Library scanning on a worker thread (TagLib): tags, album grouping via album artist,
      durations, embedded artwork extraction, dominant-color computation
- [x] JSON persistence of library, folders, and settings
- [x] Playback: queue, play/pause/next/prev, seek, volume, shuffle, repeat off/all/one,
      auto-advance (raylib music streaming; FLAC enabled in the raylib build)
- [x] FFT visualizer (Hann window, log-spaced bands, dB scaling, peak caps) fed from the
      mixed-audio tap, with bass-reactive ambient glow on Now Playing
- [x] Views: track list (virtualized), album grid, album detail, now playing
- [x] Lazy album-art texture cache with per-frame upload budget
- [x] Idle-aware frame pacing (the point of the exercise)
- [x] Dark theme ported from the web app's design tokens
- [x] **Depth layers** (the headline feature): Depth Anything v2 small (q8 ONNX, the
      same model the web app uses) runs via ONNX Runtime on a worker thread, followed
      by a faithful port of the `depthToMask` post-processing chain (median, bilateral,
      Otsu, text promotion, edge refinement, morph close/open, feather). Masks cache to
      disk as 256px grayscale PNGs; the Now Playing view renders art, then the
      full-surface visualizer (48 bars, shadow/glow/core passes), then the masked
      foreground on top — so the bars play behind the subject. The ~25 MB model
      downloads to `~/.local/share/cymaveil/models/` on first use (needs `curl`).
      Disable with `depthLayers: false` in config.json.
- [x] Background mosaic: isometric drifting grid of album art with flip /
      shrink-grow / cross-fade / fade / iris tile transitions and the radial
      vignette, ported from `AlbumArtBackground.tsx`. Drift and tile swaps only
      run during playback, so the idle state stays at zero cost. Tunables live
      in `config.json` (`mosaicEnabled`, `mosaicOpacity`, `mosaicDensity`,
      `mosaicTransition`, `mosaicFlat`) until there's a settings UI.

Not yet ported from the Electron app:

- [ ] Playlists (+ M3U8 import/export) and Favorites
- [ ] Search
- [ ] Manual mask painting (brush editor), mask import/export, batch pre-generation
- [ ] Alternative visualizer styles (contour bars, radial burst, waveform, mirrored)
      — full-surface (the default) is in
- [ ] Light theme, settings UI
- [ ] File watching / incremental rescan (currently full rescan per change)
- [ ] Playback-position restore across sessions
- [ ] MPRIS / media-key integration
- [ ] Gapless playback / crossfade

Known limitations:

- **No m4a/aac/opus playback — deliberately out of scope for now.** raylib's miniaudio
  decoders cover mp3/flac/ogg/wav, which is the bulk of any real library; such files are
  counted and skipped during scans. If it ever matters, the fix is pulling in a decoder
  library (or ffmpeg) feeding a raw `AudioStream` — not planned for this rewrite's roadmap.
- Visualizer assumes a 48 kHz output device for its frequency axis (visual-only nicety).
