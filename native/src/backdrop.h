#pragma once

#include "raylib.h"

// Frosted-glass backdrop. The ambient mosaic is rendered once into an offscreen
// scene texture; a downsampled, separable Gaussian blur of that scene is kept
// alongside it. The sharp scene is drawn as the screen base, while chrome panels
// (sidebar, queue, mini player) sample the blurred copy behind themselves and
// tint it translucently so the mosaic shows through like frosted glass.
class Backdrop {
public:
    // (Re)allocates the render targets when the framebuffer size changes. A
    // reallocation throws away the cached scene/blur, so it marks the backdrop
    // dirty (see NeedsRender) to force a refresh on the next captured frame.
    void EnsureSize(int w, int h);
    // True when the cached scene/blur can't be reused and the mosaic must be
    // re-captured this frame: after a resize-driven reallocation, or whenever the
    // caller flags the source as changed via MarkDirty(). When false, the cached
    // scene_/blur_ textures still hold a valid image and the (expensive) offscreen
    // render + Gaussian blur can be skipped entirely.
    bool NeedsRender() const { return ready_ && (dirty_ || hasRegion_); }
    // Flags the cached scene as stale (mosaic moved, art decoding, theme
    // changed, …) so the next captured frame re-renders and re-blurs.
    void MarkDirty() { dirty_ = true; }
    // Flags only part of the scene as stale (screen units). Unless something
    // marks the whole scene dirty as well, the next capture is clipped to the
    // union of these rectangles and only the blur around them is refreshed.
    void MarkDirtyRegion(Rectangle r);
    // True when the pending capture only covers the dirty region.
    bool PartialRender() const { return !dirty_ && hasRegion_; }
    // Render the scene (mosaic) between these; everything drawn lands in the
    // offscreen scene texture instead of the screen, clipped to the dirty
    // region for a partial capture. EndScene clears the dirty state, so only
    // call this pair when NeedsRender() reports the scene is stale.
    void BeginScene();
    void EndScene();  // ends capture and refreshes the blurred copy

    // Draws the sharp scene texture filling the whole screen (the base layer).
    void DrawScene() const;
    // Draws the blurred scene clipped to r, then a translucent tint on top.
    // With blur=false only the tint is drawn over the sharp scene (the web's
    // data-no-glass-blur mode).
    void DrawGlass(Rectangle r, Color tint, bool blur = true) const;
    // Same, clipped to a rounded rectangle (glass cards / popups).
    void DrawGlassRounded(Rectangle r, Color tint, float radius, bool blur = true) const;

    void Unload();
    bool Ready() const { return ready_; }

private:
    // Runs the two-pass blur from scene_ into blur_; with `partial`, only
    // where region_ can have changed it.
    void Blur(bool partial);

    int w_ = 0, h_ = 0;
    RenderTexture2D scene_{};  // full-res sharp capture of the mosaic
    RenderTexture2D blurA_{};  // downsampled ping (horizontal pass)
    RenderTexture2D blur_{};   // downsampled pong (final blurred result)
    Shader shader_{};
    int resLoc_ = -1;
    int dirLoc_ = -1;
    bool ready_ = false;
    bool dirty_ = true;  // cached scene/blur stale; re-render on next capture
    bool hasRegion_ = false;       // part of the scene stale (see MarkDirtyRegion)
    Rectangle region_{0, 0, 0, 0};  // union of the stale parts, scene pixels
};
