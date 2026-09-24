#pragma once

#include <string>
#include <vector>

#include "raylib.h"

class ArtCache;
struct Art;

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
    // `arts` is the pool of distinct covers (albums plus per-track art).
    void Rebuild(const std::vector<const Art*>& arts, const MosaicSettings& s);
    void Update(float dt, bool playing, const MosaicSettings& s);
    // What changed in the composed mosaic since the last Draw().
    struct Change {
        bool full = false;     // recompose everything
        bool partial = false;  // only `region` (screen space) changed
        Rectangle region{0, 0, 0, 0};
    };
    // Brings the cached layer of settled tiles up to date. Call every frame,
    // outside any texture mode, before capturing the backdrop. Everything
    // needs recomposing when the layer was re-rendered or the drift has moved
    // far enough to show; while only tiles are mid-transition, just their
    // rectangles do. No change means the previous capture is still exact.
    Change Prepare(Rectangle screen, ArtCache& art, const MosaicSettings& s, Color bg);
    // Composes the mosaic: the cached layer shifted by the drift since it was
    // rendered, the tiles that are mid-transition drawn live on top, and the
    // vignette. `partial` refreshes the region Prepare() reported (the caller
    // clips to it) at the drift of the last full composition, so it lines up
    // with the rest of the capture.
    void Draw(Rectangle screen, ArtCache& art, const MosaicSettings& s, Color bg, bool partial);
    // True when Draw() covers the whole screen opaquely (no clear needed).
    bool CoversScreen() const { return layer_.id != 0; }
    // Forces a layer re-render (e.g. artwork finished decoding).
    void MarkDirty() { layerDirty_ = true; }
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

    // The grid's placement for a given screen: CSS rotateX/rotateZ under
    // ortho, i.e. screen = center + squash * rotate(plane + drift).
    struct Geometry {
        float gridW, gridH, tileSz, step;
        Vector2 center;
        float squash, cosA, sinA;
        bool flat;
    };

    static float Duration(Tr tr);
    Geometry Layout(Rectangle screen, const MosaicSettings& s) const;
    Vector2 Drift(const Geometry& g) const;                     // plane units
    static Vector2 PlaneToScreen(const Geometry& g, Vector2 d);  // linear part only
    // Draws either the settled or the mid-transition tiles at `drift`, culled
    // to `bounds` (screen space).
    void DrawTiles(const Geometry& g, Vector2 drift, Rectangle bounds, ArtCache& art, float opacity,
                   bool active) const;
    void DrawTile(const Tile& tile, Rectangle rc, ArtCache& art, float opacity) const;
    const Texture2D* Tex(int artIdx, ArtCache& art) const;

    std::vector<std::string> artIds_;  // artwork file paths
    std::vector<Tile> tiles_;
    int columns_ = 0;
    int rows_ = 0;
    float driftT_ = 0;
    float nextAnim_ = 3.0f;
    bool drifting_ = false;  // grid drifted this frame (playing + enabled)

    // Settled tiles over the background, rendered at layerDrift_ and padded
    // by a margin on every side so it can slide with the drift. Tiles that are
    // mid-transition are left out and drawn live, so only the start and end
    // of a swap re-render it.
    RenderTexture2D layer_{};
    bool layerDirty_ = true;
    Vector2 layerDrift_{0, 0};
    float layerOpacity_ = -1.0f;
    bool layerFlat_ = false;
    Color layerBg_{0, 0, 0, 0};
    Vector2 drawnShift_{0, 0};  // layer shift at the last full Draw()
    Vector2 drawnDrift_{0, 0};  // drift at the last full Draw()

    Texture2D vignette_{};
    Color vignetteBg_{0, 0, 0, 0};  // bg the cached vignette was baked with
};
