#pragma once

#include "raylib.h"

// Shared separable Gaussian blur (9-tap, GL 330). One shader instance serves
// the frosted-glass backdrop and the Now Playing art transition blur. The
// blur runs in a render-target ping-pong at whatever resolution the caller
// allocates; `spread` widens the tap spacing for a heavier result.
class GaussianBlur {
public:
    // Blurs `src` (a texture, read with the given source rect) into `dst`
    // through `tmp` (both dst/tmp must be the same size). `passes` iterations
    // of H+V. Must be called outside BeginDrawing's scissor/shader state.
    void Run(const Texture2D& src, Rectangle srcRect, RenderTexture2D& tmp, RenderTexture2D& dst,
             float spread, int passes = 1);
    void Unload();

private:
    void Ensure();
    Shader shader_{};
    int resLoc_ = -1;
    int dirLoc_ = -1;
};
