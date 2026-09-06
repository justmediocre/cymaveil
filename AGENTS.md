# Working on Cymaveil

Electron + React + Vite music player. The renderer lives in `src/`, the main process
in `electron/`, and `electron-builder` config sits in the `build` field of `package.json`.

## Commands

| Task | Command |
| --- | --- |
| Dev (Vite + Electron) | `npm run electron:dev` |
| Typecheck | `npm run typecheck` |
| Renderer build | `npm run build` |
| Package for this OS | `npx electron-builder --linux` (or `--win` / `--mac`) |

## Releases

Releases are built by `.github/workflows/release.yml` on Linux, Windows and macOS
runners — free and unmetered because the repo is public. Cross-building locally does
not work: Windows needs wine for `rcedit`, and macOS DMGs can only be built on macOS.

To cut one: bump the version in `package.json`, commit, then push a `v*` tag. The
workflow builds all six artifacts, attaches them to a **draft** release, and marks it
prerelease automatically when the tag contains `-beta` or `-rc`. Review the assets,
then publish.

Builds are unsigned, so first launch is blocked by Gatekeeper on macOS
(`xattr -cr /Applications/Cymaveil.app`) and warned about by SmartScreen on Windows.
Keep those instructions in the release notes — without them people assume the app is
broken.

### Building a `.deb` locally on Fedora

`electron-builder`'s bundled `fpm` ships a portable Ruby linked against
`libcrypt.so.1`, which Fedora no longer provides. Install `libxcrypt-compat`, or let
CI build the deb. Every other target builds fine locally.

## Verifying a change in the real app

The best signal is running the packaged app, not just the tests. On a Linux box with
a desktop session (`echo $DISPLAY` is non-empty), launch it with an isolated profile
and remote debugging:

```bash
./release/Cymaveil-<version>.AppImage \
  --user-data-dir=/tmp/cym-test-profile \
  --remote-debugging-port=9222
```

`--user-data-dir` keeps your real config untouched *and* gives you a genuine
fresh-install state, which is what you want when testing default settings. Then drive
the renderer over the Chrome DevTools Protocol — Node 24 has a built-in `WebSocket`,
so a short script against `http://127.0.0.1:9222/json/list` is enough to evaluate
expressions, click elements and call `Page.captureScreenshot`.

On KDE/Wayland, `spectacle -b -n -f -o shot.png` captures the whole screen;
`Page.captureScreenshot` gives a cleaner window-only image.

### Test data for depth layers

Use **`~/Music/Ponies at Dawn - Memories`**. Its cover art produces by far the most
distinct foreground/background split, so the visualizer clearly reads as sitting
*inside* the image. Most other albums give a muddy or barely visible result and will
make a working build look broken. (`Vylet Pony - ANTONYMPH` is smaller — 4 tracks vs
60 — so it is faster to scan, but a poor visual example.)

To confirm segmentation actually ran rather than just checking the toggle, inspect the
`cymaveil-segmentation-cache` IndexedDB database: a completed run leaves entries in
its `masks` and `contour-data` stores. The Depth Anything v2 model is downloaded on
first use and cached under `transformers-cache` in Cache Storage.

### Gotchas that cost real time

- **`contextBridge` deep-freezes `window.electronAPI`.** Assigning over
  `selectFolder` to stub the folder picker silently no-ops, and the app opens a real
  native dialog that blocks the scan forever. To seed a library without a dialog, call
  `scanMusicFolder(path)` and pass the result to `saveLibrary({ albums, tracks, folders })`,
  then reload.
- **Use CDP `Page.reload`, not `location.reload()` via `Runtime.evaluate`.** The
  latter frequently does not take effect, which looks exactly like the library failing
  to load on startup.
- **Single-instance lock.** `electron/main.js` calls `requestSingleInstanceLock()` and
  quits if it loses. A `SIGKILL`ed instance leaves a stale `SingletonLock` in the
  profile, so the next launch exits immediately after printing "DevTools listening".
  Use a fresh `--user-data-dir` per run.
- **Killing an AppImage.** `pkill -f Cymaveil` matches the outer wrapper only; the
  real processes are `/tmp/.mount_Cym*/cymaveil`. Match on `[.]mount_Cym` — and note
  the bracket, since an unbracketed `pkill -f` pattern also matches your own shell's
  command line and kills it.

## Native (raylib) rewrite

`native/` is a C++20/raylib reimplementation on the `raylib-rewrite` branch; see
`native/README.md` for the build (the `.devcontainer/` image is the intended
toolchain — on a host without cmake/dev headers, build inside it with podman:
`podman build -t cymaveil-native-dev -f .devcontainer/Dockerfile .devcontainer`
then `podman run --rm --userns=keep-id -v "$PWD":/work:Z -w /work cymaveil-native-dev
cmake --build native/build`). The resulting binary runs on the host. To verify a
change visually, launch with `XDG_DATA_HOME=/tmp/some-dir` (fresh cache, keeps your
real one untouched), a music folder as the argument, and `--play --view now --shot
/tmp/shot.png` to capture a screenshot ~2 s in; `--view` also takes
`library|albums|album|playlists|settings:visuals` etc. The first run scans the
folder, so let one run finish (or `timeout 30`) before taking shots. `--shot`
disables HiDPI (so shots are exactly the requested size) unless `--fullscreen`
is also passed, so `--fullscreen --shot /tmp/fs.png` captures the fullscreen
HiDPI layout at the display's physical size; there is no way to inject F11 on
Wayland. For a windowed HiDPI check run without `--shot` and grab the screen
with `spectacle -b -n -f -o shot.png` (needs an interactive desktop session:
from a non-interactive shell the portal prompt never appears and the capture
comes back blank). Stop it with `pkill -x cymaveil`; a `pkill -f` pattern also
matches the shell that launched it.

## Settings defaults

Renderer visual defaults live in `DEFAULTS` in `src/lib/visualSettingsStore.ts` and
persist to `localStorage`. `load()` spreads stored values over the defaults, so
changing a default only affects fresh installs — existing users keep whatever they
had. Say so explicitly in release notes when flipping one.
