#pragma once

#include "raylib.h"

// Album palette extraction, ported from the web app's colorExtractor.ts:
// bucket-quantized sampling -> dominant (most frequent, darkened for
// backgrounds), accent (most vivid, vibrancy-boosted), and an optional
// secondary accent with a distinct hue (drives the two-tone visualizer).
struct AlbumColors {
    Color dominant{110, 110, 122, 255};
    Color accent{212, 165, 116, 255};
    Color accentSecondary{0, 0, 0, 255};
    bool hasSecondary = false;
};

// rgba: 64x64 RGBA pixels (the extraction sample size used by the web app).
// Returns false if no usable colors were found (out is left untouched).
bool ExtractAlbumColors(const unsigned char* rgba, AlbumColors* out);
