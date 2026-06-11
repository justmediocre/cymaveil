#pragma once

#include <string>
#include <vector>

#include "raylib.h"

class ArtCache;
class Library;
struct Album;

struct MosaicSettings {
    bool enabled = true;
    float opacity = 0.18f;
    int density = 8;                     // grid columns; rows = ceil(density * 1.5)
    std::string transition = "random";   // flip | shrink-grow | cross-fade | fade | iris | random
    bool flat = false;                   // disable the isometric tilt
};

// Ambient "living album art" background, ported from AlbumArtBackground.tsx:
// a tilted, slowly drifting grid of album covers where a center-weighted
// random tile periodically swaps to a different artwork.
class Mosaic {
public:
    // Reassigns artwork to the tile grid. Call on startup and after scans.
    void Rebuild(const std::vector<Album>& albums, const MosaicSettings& s);
    void Update(float dt, bool playing, const MosaicSettings& s);
    void Draw(Rectangle screen, ArtCache& art, const Library& lib, const MosaicSettings& s,
              Color bg);
    // Manually animate one tile (hotkey/testing).
    void Trigger(const MosaicSettings& s);
    bool Animating() const;
    void Unload();

private:
    enum class Tr { Flip, ShrinkGrow, CrossFade, Fade, Iris };

    struct Tile {
        int front = 0;   // indices into artIds_
        int back = 0;
        float t = 0;     // animation progress 0..1
        bool active = false;
        Tr tr = Tr::Flip;
    };

    static float Duration(Tr tr);
    void DrawTile(const Tile& tile, Rectangle rc, ArtCache& art, const Library& lib,
                  float opacity) const;
    const Texture2D* Tex(int artIdx, ArtCache& art, const Library& lib) const;

    std::vector<std::string> artIds_;  // album ids that have artwork
    std::vector<Tile> tiles_;
    int columns_ = 0;
    int rows_ = 0;
    float driftT_ = 0;
    float nextAnim_ = 3.0f;

    Texture2D vignette_{};
};
