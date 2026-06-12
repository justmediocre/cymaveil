#pragma once

#include "raylib.h"

// Frosted-glass backdrop. The ambient mosaic is rendered once into an offscreen
// scene texture; a downsampled, separable Gaussian blur of that scene is kept
// alongside it. The sharp scene is drawn as the screen base, while chrome panels
// (sidebar, queue, mini player) sample the blurred copy behind themselves and
// tint it translucently so the mosaic shows through like frosted glass.
class Backdrop {
public:
    // (Re)allocates the render targets when the framebuffer size changes.
    void EnsureSize(int w, int h);
    // Render the scene (mosaic) between these; everything drawn lands in the
    // offscreen scene texture instead of the screen.
    void BeginScene();
    void EndScene();  // ends capture and refreshes the blurred copy

    // Draws the sharp scene texture filling the whole screen (the base layer).
    void DrawScene() const;
    // Draws the blurred scene clipped to r, then a translucent tint on top.
    void DrawGlass(Rectangle r, Color tint) const;

    void Unload();
    bool Ready() const { return ready_; }

private:
    void Blur();  // runs the two-pass blur from scene_ into blur_

    int w_ = 0, h_ = 0;
    RenderTexture2D scene_{};  // full-res sharp capture of the mosaic
    RenderTexture2D blurA_{};  // downsampled ping (horizontal pass)
    RenderTexture2D blur_{};   // downsampled pong (final blurred result)
    Shader shader_{};
    int resLoc_ = -1;
    int dirLoc_ = -1;
    bool ready_ = false;
};
