# Contributing to Cymaveil

Thanks for your interest in contributing! Here are a few guidelines to keep things smooth.

## License

By submitting a contribution to this project, you agree that your contribution is licensed under the [MIT License](LICENSE).

## Reporting Bugs

Open an issue and include:

- Steps to reproduce the problem
- What you expected to happen vs. what actually happened
- OS and app version

## Suggesting Features

Open an issue describing the feature and why it would be useful. Keep it focused — one idea per issue.

## Development Setup

### Prerequisites

- [Node.js](https://nodejs.org/) 18 or later
- npm (comes with Node.js)

### Getting started

```bash
git clone https://github.com/justmediocre/cymaveil.git
cd cymaveil
npm install
npm run electron:dev
```

This starts the Vite dev server and launches Electron with hot reload.

### Using the dev container

If you'd rather not install Node and Electron's system libraries yourself, open the
repo in the included [dev container](https://containers.dev/) (in VS Code, run
**Dev Containers: Reopen in Container**). Dependencies are installed on first start.

- `npm run typecheck`, `npm run build` and `npm run dist:linux` work as usual. Packages
  land in `release/` on your own machine.
- Electron draws to a virtual desktop. Open <http://localhost:6080>, click **Connect**,
  then run `npm run electron:dev` in the container terminal.
- The container has no GPU or sound card, so the app renders in software and you won't
  hear anything. That's fine for UI work but not for judging performance.
- Windows and macOS packages can't be built in the container. Push a tag and let the
  release workflow build them.

### Useful commands

| Command | What it does |
|---|---|
| `npm run electron:dev` | Start dev server + Electron with HMR |
| `npm run build` | Type-check and build the renderer |
| `npm run typecheck` | Run TypeScript type checking only |
| `npm run dist` | Package for Windows (NSIS + portable) |
| `npm run dist:linux` | Package for Linux (AppImage + deb) |
| `npm run dist:mac` | Package for macOS (DMG) |

### Project structure

```
src/
├── contexts/          # React contexts (library, playlist, playback)
├── hooks/             # Custom hooks (audio, colors, segmentation, media session)
├── lib/
│   ├── segmentation/  # Depth estimation, mask generation, caching
│   └── ...            # Color extraction, edge detection, audio analysis
├── components/
│   ├── layout/        # App shell, title bar, queue panel
│   ├── views/         # Route-level views (library, albums, now playing, etc.)
│   ├── settings/      # Per-tab settings components (library, visuals, depth layers, about)
│   └── *.tsx          # Shared components (album art, controls, visualizer, etc.)
electron/
├── main.js            # Electron lifecycle and IPC
├── preload.cjs        # Secure bridge between renderer and main
├── musicScanner.js    # File system scanning and metadata extraction
├── libraryStore.js    # Persistent storage (electron-store)
├── playlistFile.js    # M3U8 playlist import/export
├── maskOverrideFile.js # Mask override import/export
└── fileWatcher.js     # Live file watching with chokidar
```

## Pull Requests

1. Fork the repo and create a branch from `main`.
2. Keep changes focused — one fix or feature per PR.
3. Make sure the project builds cleanly (`npm run build`) and passes typechecking (`npm run typecheck`).
4. Write a clear PR description explaining what changed and why.
