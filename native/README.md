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
configure — no system installs needed beyond the windowing/audio dev packages (plus
`libdbus-1-dev` for MPRIS; optional, the build falls back to a stub without it).

Audio inside the dev container is forwarded to the host's PulseAudio/PipeWire socket
(`$XDG_RUNTIME_DIR/pulse` is mounted; miniaudio picks it up via `PULSE_SERVER`).

## Usage

- **Drag & drop** a music folder onto the window to add it to the library (or pass folders
  as CLI args). Rescans happen on a background thread. Dropping a `.m3u`/`.m3u8` file
  imports it as a playlist instead (entries are matched against the library by path).
- Library / Albums / Playlists / Now Playing views via the sidebar or keys **1 / 2 / 3 / 4**;
  the sidebar **Settings** entry (or **,**) opens the theme picker.
- **Ctrl+F** (or the sidebar **Search** entry) filters across tracks, albums, and artists;
  **Esc** clears the query, then exits back to the Library.
- **Right-click any track row** for the context menu: play, toggle Favorites, add/remove
  Now Playing, add to a playlist (or start a new one from the track).
- **Q** (or the list button in either player bar) toggles the queue panel: the active
  queue in play order — click to jump, hover for per-row remove — or the parked Now
  Playing list with Play/Clear when nothing is queued.
- **Space** play/pause · **←/→** seek ±5s · **Ctrl+←/→** prev/next · **↑/↓** volume ·
  **S** shuffle · **R** repeat cycle · **Q** queue panel · **,** settings ·
  **B** animate a mosaic tile · **Esc** back · **F3** debug overlay.

Library cache, settings, playlists, and extracted album art live in
`~/.local/share/cymaveil/`. Playlist exports are written to `~/Music/<name>.m3u8`.

**Desktop integration:** copy `native/cymaveil.desktop` to
`~/.local/share/applications/` and point `Exec`/`Icon` at the built binary and
`build/icon.png`. The window sets app id / WM_CLASS `cymaveil` (via a raylib
patch, see `cmake/patch-raylib-appid.cmake`), and the desktop file is what lets
the desktop attach an icon to the window on Wayland and bind the MPRIS player
to the taskbar entry — without it, e.g. KDE's task manager won't show hover
media controls, because the matcher bails on tasks with no launcher URL before
it ever compares PIDs.

Dev/testing flags: `--play` (autoplay the library on launch),
`--view library|albums|playlists|now` (start on a view), `--import <file.m3u8>` (import a
playlist on launch), `--shot <path>` (capture a screenshot ~2s in, with debug overlay).

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
- [x] Mini player while browsing (ported from `MiniPlayer.tsx`): compact bar with
      scrubbable hairline progress, play/pause + next, click to expand into Now
      Playing; the full transport lives on the Now Playing view
- [x] Vinyl disc behind the Now Playing art (groove rings, rotating light-catch
      sheen at 1.8s/rev, accent-colored center label): slides out while playing,
      retracts on pause, and album changes sequence retract → art fade/scale
      entrance → extend, with faster timings on manual skips — ported from
      `AlbumArt.tsx`. Disable with `vinylDisc: false` in config.json.
- [x] Background mosaic: isometric drifting grid of album art with flip /
      shrink-grow / cross-fade / fade / iris tile transitions and the radial
      vignette, ported from `AlbumArtBackground.tsx`. Drift and tile swaps only
      run during playback, so the idle state stays at zero cost. Tunables live
      in `config.json` (`mosaicEnabled`, `mosaicOpacity`, `mosaicDensity`,
      `mosaicTransition`, `mosaicFlat`) until there's a settings UI.
- [x] Playlists: Favorites and Now Playing system playlists plus user playlists
      (inline rename, two-step delete), persisted to `playlists.json`; M3U8 import
      via file drop or `--import` and export to `~/Music`; a right-click track menu
      for building lists; and the collapsible queue panel (Q), which tracks the
      player's queue source so Now Playing edits stay in sync with live playback —
      ported from `usePlaylists.ts` / `QueuePanel.tsx`. One deliberate divergence:
      the web app's "queue-building click mode" setting wasn't carried over —
      clicks always play in context, and queue building goes through the menu.
- [x] MPRIS / media keys: `org.mpris.MediaPlayer2.cymaveil` on the session bus
      with full metadata (incl. cover art), transport/seek/position, and
      writable Volume/Shuffle/LoopStatus — media keys arrive through this on
      modern desktops. The D-Bus worker thread pokes the GLFW event loop, so
      remote commands work even while the app is blocked idle on OS events.
      Builds against libdbus when present (stubbed out otherwise; the dev
      container has it). Disable with `mpris: false` in config.json.
- [x] Playback-position restore across sessions: the live queue (incl. shuffle
      order and queue source), current track, and play position persist to
      `session.json` — on quit and checkpointed every 10 s while playing, so a
      crash loses at most a few seconds. The next launch cues everything back
      up paused at the saved position; tracks that left the library are
      dropped from the restored queue. `--play` resumes a restored session
      instead of restarting the library from the top.
- [x] Search: a live filter across track titles, track artists, and album
      titles/artists. The sidebar **Search** entry (or **Ctrl+F**) opens an
      always-focused box; matches are split into an album grid (click to open
      the album) and a track table (click to play, right-click for the menu).
- [x] Light theme + theme preference: the dark palette gained a light
      counterpart (ported from `index.css`), chosen from a new **Settings** view
      in the sidebar (or the **,** shortcut). The preference — Light / Dark /
      System — persists to `config.json`; **System** follows the desktop's
      color scheme via the XDG settings portal (`org.freedesktop.appearance`,
      reusing the libdbus dependency), falling back to dark where no portal is
      present.

Not yet ported from the Electron app:

- [ ] Manual mask painting (brush editor), mask import/export, batch pre-generation
- [ ] Alternative visualizer styles (contour bars, radial burst, waveform, mirrored)
      — full-surface (the default) is in
- [ ] Fuller settings UI: the Settings view so far holds only the theme picker;
      the remaining `config.json` tunables (mosaic, depth layers, library
      folders) still need surfacing
- [ ] File watching / incremental rescan (currently full rescan per change)
- [ ] Gapless playback / crossfade

Known limitations:

- **No m4a/aac/opus playback — deliberately out of scope for now.** raylib's miniaudio
  decoders cover mp3/flac/ogg/wav, which is the bulk of any real library; such files are
  counted and skipped during scans. If it ever matters, the fix is pulling in a decoder
  library (or ffmpeg) feeding a raw `AudioStream` — not planned for this rewrite's roadmap.
- Visualizer assumes a 48 kHz output device for its frequency axis (visual-only nicety).
