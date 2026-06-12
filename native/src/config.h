#pragma once

#include <string>
#include <vector>

// User settings persisted across sessions (volume, shuffle/repeat, etc.).
// Library folders live in the library cache; this is everything else.
struct Config {
    float volume = 0.8f;
    bool shuffle = false;
    int repeat = 0;  // 0=off 1=all 2=one

    // Depth layers: ML foreground masks so the visualizer plays behind the
    // album art subject. First use downloads the ~25 MB model.
    bool depthLayers = true;
    // Vinyl disc that slides out from behind the Now Playing album art
    bool vinylDisc = true;
    // Collapsible queue panel on the right edge (toggled with Q)
    bool queuePanel = false;
    // MPRIS D-Bus interface (desktop media controls / media keys)
    bool mpris = true;

    // Color theme preference: "system" follows the desktop's color-scheme
    // (via the XDG portal), "light"/"dark" force a palette.
    std::string theme = "system";

    // Background mosaic (no settings UI yet; editable in config.json)
    bool mosaicEnabled = true;
    float mosaicOpacity = 0.18f;
    int mosaicDensity = 8;  // grid columns
    std::string mosaicTransition = "random";
    bool mosaicFlat = false;

    void Load();
    void Save() const;
};
