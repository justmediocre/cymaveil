#pragma once

#include <string>
#include <vector>

// User settings persisted across sessions (volume, shuffle/repeat, etc.).
// Library folders live in the library cache; this is everything else.
// Visual keys mirror the web app's visualSettingsStore defaults.
struct Config {
    float volume = 0.8f;
    bool shuffle = false;
    int repeat = 0;  // 0=off 1=all 2=one

    // Color theme preference: "system" follows the desktop's color-scheme
    // (via the XDG portal), "light"/"dark" force a palette.
    std::string theme = "system";

    // ── Effects (VisualsTab) ──
    bool canvasVisualizer = true;  // frequency bars on album art
    bool glassBlur = true;         // frosted backdrop behind panels
    bool ambientGlow = true;       // accent underglow around the art
    bool bassShake = true;         // bass-hit zoom on the art
    bool vinylDisc = true;         // spinning record behind the art
    bool mosaicEnabled = true;     // background mosaic
    bool mosaicFlat = false;
    float mosaicOpacity = 0.18f;
    int mosaicDensity = 8;  // grid columns
    std::string mosaicTransition = "random";

    // ── Visualizer ──
    std::string visualizerStyle = "full-surface";  // or "random"
    int visualizerIntensity = 65;                  // 10..100

    // ── Depth layers ──
    // ML foreground masks so the visualizer plays behind the album art
    // subject. First use downloads the ~25 MB model.
    bool depthLayers = true;

    // ── Playback / integration ──
    bool mpris = true;   // MPRIS D-Bus interface (desktop media controls)

    // ── Layout ──
    bool sidebarOpen = true;
    bool queuePanel = false;

    void Load();
    void Save() const;
};
