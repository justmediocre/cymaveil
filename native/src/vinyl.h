#pragma once

#include "raylib.h"

// The vinyl record that slides out from behind the Now Playing album art,
// ported from AlbumArt.tsx: radial groove rings, a rotating light-catch
// sheen, rim highlight and a two-tone centre label with a spindle hole. Pure
// renderer — the slide/spin state lives in ArtView.
class Vinyl {
public:
    // Draws the disc centred at `c` with the given diameter, rotated by
    // spinDeg, at `alpha`. Label colours: accent -> dominant radial.
    void Draw(Vector2 c, float diameter, float spinDeg, float alpha, Color labelAccent,
              Color labelDominant);
    void Unload();

private:
    void EnsureTextures();
    Texture2D body_{};
    Texture2D sheen_{};
};
