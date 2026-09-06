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

Dependencies (raylib, TagLib, nlohmann/json, ONNX Runtime) are pulled via CMake
`FetchContent` on first configure — no system installs needed beyond the windowing/audio dev
packages (plus `libdbus-1-dev` for MPRIS; optional, the build falls back to a stub without it).

Audio inside the dev container is forwarded to the host's PulseAudio/PipeWire socket
(`$XDG_RUNTIME_DIR/pulse` is mounted; miniaudio picks it up via `PULSE_SERVER`).

### Windows (MSVC)

Needs Visual Studio 2022 with the **Desktop development with C++** workload (which
includes CMake), then from the repo root:

```bat
cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64
cmake --build native/build --config Release
native\build\cymaveil\Release\Cymaveil.exe
```

Dependencies come through `FetchContent` exactly as on Linux — the configure step
automatically selects the win-x64 ONNX Runtime build, and `onnxruntime.dll` is copied
next to the executable as a post-build step. The Linux-only features (inotify file
watching, D-Bus/MPRIS, the freedesktop appearance portal) compile to no-op stubs on
Windows; everything else — playback, library, visualizer, depth layers — is shared.

### macOS

Needs the Xcode Command Line Tools (`xcode-select --install`) and CMake. From the
repo root:

```sh
cmake -S native -B native/build -G Ninja
cmake --build native/build
open native/build/cymaveil/cymaveil.app
```

The build produces a `cymaveil.app` bundle (universal2 ONNX Runtime, so one build
runs on both Apple Silicon and Intel; the dylib is bundled in `Contents/MacOS` and
resolved via an `@loader_path` rpath). The same Linux-only features stub out here.

To package a disk image:

```sh
cd native/build && cpack   # → Cymaveil-<version>.dmg
```

The `.dmg` is **unsigned** — on first launch macOS Gatekeeper will block it.
Right-click the app → **Open** (then confirm), or clear the quarantine bit:
`xattr -dr com.apple.quarantine /Applications/cymaveil.app`.

## Usage

- **Drag & drop** a music folder onto the window to add it to the library (or pass folders
  as CLI args, or add a path under **Settings → Music Folders**). Scans happen on a
  background thread and are incremental — unchanged files are reused from the cache, so
  only new/edited files are re-read. Watched folders are also tracked live (inotify), so
  the library updates on its own when files are added, changed, or removed on disk.
  Dropping a `.m3u`/`.m3u8` file imports it as a playlist instead (entries are matched
  against the library by path).
- The layout mirrors the Electron app: a collapsible sidebar (Now Playing /
  Search / Library / Albums / Favorites / Playlists / Settings), a title bar
  with the sidebar, queue, fullscreen and theme toggles, the mini player while
  browsing, and the collapsible queue panel on the right. Keys **1 / 2 / 3 / 4**
  jump to Library / Albums / Playlists / Now Playing, **,** opens Settings.
- **Ctrl+F** (or the sidebar **Search** entry) filters across tracks, albums, and artists;
  **Esc** clears the query, then exits back to the Library.
- Track rows show hover actions like the web app: a heart (Favorites), **+**
  (add-to-playlist menu) and, in playlists, **×** (remove). Right-clicking a row
  opens the same menu.
- **Ctrl+T** (or **Q**, or the title-bar list button) toggles the queue panel: the
  active queue in play order — click to jump — or the parked Now Playing list
  with Play/Clear when nothing is queued.
- **Space** play/pause · **←/→** seek ±5s · **Ctrl+←/→** prev/next · **↑/↓** volume ·
  **S** shuffle · **R** repeat cycle · **Ctrl+T** queue panel · **,** settings ·
  **B** paint the depth mask by hand · **F11** fullscreen (Now Playing goes
  immersive — sidebar hidden, cursor and controls fade after a brief idle;
  **Esc** exits) · **Ctrl+Shift+B** animate a mosaic tile · **Esc** back ·
  **F3** debug overlay. Clicking the Now Playing cover cycles the visualizer style.

Library cache, settings, playlists, and extracted album art live in
`~/.local/share/cymaveil/` (`%APPDATA%\cymaveil\` on Windows,
`~/Library/Application Support/cymaveil/` on macOS). Playlist exports are
written to `~/Music/<name>.m3u8` (`%USERPROFILE%\Music\` on Windows).

**Desktop integration:** copy `native/cymaveil.desktop` to
`~/.local/share/applications/` and point `Exec`/`Icon` at the built binary and
`build/icon.png`. The window sets app id / WM_CLASS `cymaveil` (via a raylib
patch, see `cmake/patch-raylib-appid.cmake`), and the desktop file is what lets
the desktop attach an icon to the window on Wayland and bind the MPRIS player
to the taskbar entry — without it, e.g. KDE's task manager won't show hover
media controls, because the matcher bails on tasks with no launcher URL before
it ever compares PIDs.

Dev/testing flags: `--play` (autoplay the library on launch),
`--view search|library|albums|album|favorites|playlists|now|settings[:visuals|:depth|:about]|brush`
(start on a view; `album` opens the first album's detail), `--import <file.m3u8>`
(import a playlist on launch), `--fullscreen` (enter fullscreen on launch, as F11
would), `--shot <path>` (capture a screenshot ~2s in, with debug overlay). Point
`XDG_DATA_HOME` at a scratch directory to keep test runs out of your real library
cache.

The library cache format is versioned (`"version": 2` in `library.json`, since
artwork moved to content-hashed files with per-track covers); an older cache is
discarded and rebuilt from the same folders on the next launch.

## Status

Done in this first slice:

- [x] Library scanning on a worker thread (TagLib): tags, album grouping via album artist,
      durations, embedded artwork extraction, dominant-color computation. Scans are
      **incremental** — each track caches its file mtime, so a rescan reuses unchanged
      files and only re-parses what actually changed (artwork/palette are carried over)
- [x] **File watching** (inotify, Linux): a worker thread watches the library folders and
      subtrees and debounces filesystem events into an incremental rescan, so adds, edits,
      and deletions on disk show up without a manual refresh. Pokes the GLFW event loop so
      the idle main loop wakes only when something actually moves; a no-op stub elsewhere
- [x] **Folder management** in Settings: list the watched folders, remove any, or add one
      by typing/pasting a path (alongside drag & drop)
- [x] JSON persistence of library, folders, and settings
- [x] Playback: queue, play/pause/next/prev, seek, volume, shuffle, repeat off/all/one,
      auto-advance (raylib music streaming; FLAC enabled in the raylib build)
- [x] Visualizer: an emulation of the web app's Web Audio analyser (fftSize 512,
      smoothingTimeConstant 0.4, byte frequency data over −100..−30 dB) fed from
      the mixed-audio tap, driving faithful ports of all five styles — full
      surface (default), mirrored bars, radial burst, waveform and contour bars
      (edge-contour extraction ported from `edgeDetector.ts` / `contourPath.ts`).
      Style, intensity and 'random' live in Settings → Visuals; clicking the
      cover cycles styles.
- [x] The Electron app's look and feel, ported view by view from the React
      components and `index.css`: the same design tokens, the same three
      typefaces embedded at build time (Outfit body, Bricolage Grotesque display,
      JetBrains Mono numerals), the same icon set, frosted-glass panels, hover
      and tap transitions (CSS-transition / Motion-spring stand-ins in `ui.cpp`),
      the sidebar with the letterpress wordmark and Up Next, the title bar, the
      alphabetised Library with its letter rail, album cards with the hover play
      button, playlist rows, the tabbed Settings, and the Now Playing glass
      controls panel with marquee title, hover-revealed seek thumb and the
      hover volume popover.
- [x] Lazy album-art texture cache with per-frame upload budget
- [x] Idle-aware frame pacing (the point of the exercise)
- [x] Dark and light themes ported from the web app's design tokens
- [x] **Depth layers** (the headline feature): Depth Anything v2 small (q8 ONNX, the
      same model the web app uses) runs via ONNX Runtime on a worker thread, followed
      by a faithful port of the `depthToMask` post-processing chain (median, bilateral,
      Otsu, text promotion, edge refinement, morph close/open, feather). Masks cache to
      disk as 256px grayscale PNGs; the Now Playing view renders art, then the
      full-surface visualizer (48 bars, shadow/glow/core passes), then the masked
      foreground on top — so the bars play behind the subject. The ~25 MB model
      downloads to the data dir's `models/` on first use (via libcurl-style fetch on
      Linux, `URLDownloadToFile` on Windows). Disable with `depthLayers: false` in config.json.
- [x] Mini player while browsing (ported from `MiniPlayer.tsx`): compact bar with
      scrubbable hairline progress, play/pause + next, click to expand into Now
      Playing; the full transport lives on the Now Playing view
- [x] Now Playing art stack (`artview.cpp`, ported from `AlbumArt.tsx`): the
      cover with rounded corners, resting drop shadow, accent underglow while
      playing, reflection blob and hover inner glow; the vinyl disc that slides
      out while playing; album changes sequenced as retract → old cover fades,
      shrinks and blurs out → new cover blurs in → extend (faster on manual
      skips, pre-fired before a track ends on a different album); per-track
      cover changes within an album swap in place; and the bass-hit zoom. The
      art, visualizer and depth foreground composite into one offscreen target
      so the corner clipping, blur and zoom apply to them together.
- [x] Background mosaic: isometric drifting grid of album art with flip /
      shrink-grow / cross-fade / fade / iris tile transitions and the radial
      vignette, ported from `AlbumArtBackground.tsx`. Drift and tile swaps only
      run during playback, so the idle state stays at zero cost. The pool
      includes per-track covers, and every tunable is in Settings → Visuals.
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

- [x] Per-track album art (ported from the Electron app's `61ebb63` / `58ed0cd`):
      every file's embedded picture is extracted and stored under a content
      hash, so identical covers share one file; a track whose picture differs
      from its album's keeps it as its own art, and everything that shows a
      track — lists, mini player, Up Next, MPRIS, the mosaic pool, the depth
      masks and Now Playing — resolves the track's art before the album's.
- [x] Manual mask painting (brush editor), opened with **B** or the button that
      appears over the cover; masks are keyed by the artwork file so they follow
      per-track covers too.
- [x] Settings view with the web app's tabs: Library (folders), Playback (MPRIS),
      Visuals (theme, effects, mosaic, visualizer), Depth Layers, About.

Not yet ported from the Electron app:

- [ ] Mask import/export and batch pre-generation of depth masks
- [ ] Gapless playback / crossfade
- [ ] Copy and paste text in inputs

Known limitations:

- **No m4a/aac/opus playback — deliberately out of scope for now.** raylib's miniaudio
  decoders cover mp3/flac/ogg/wav, which is the bulk of any real library; such files are
  counted and skipped during scans. If it ever matters, the fix is pulling in a decoder
  library (or ffmpeg) feeding a raw `AudioStream` — not planned for this rewrite's roadmap.
- Visualizer assumes a 48 kHz output device for its frequency axis (visual-only nicety).
