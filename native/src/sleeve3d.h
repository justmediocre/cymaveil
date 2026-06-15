#pragma once

#include "raylib.h"

// Renders the album sleeve mid-flip as an actual 3D slab: a thin textured box
// turned about its vertical axis, front cover on one face and the incoming
// cover on the other, worn "patina" cardboard on the four edges. A small GLSL
// pass does directional lighting plus a gloss highlight that slides across the
// cover as it turns. Output lands in an offscreen target the Now Playing view
// cross-fades over the flat 2D cover during the turn.
class Sleeve3D {
public:
    // front/back: the outgoing and incoming cover textures (nullptr -> skip).
    // edgeFrom/edgeTo: album dominant colors, blended for the patina tint.
    // flip: 0..1 turn progress. Renders into the internal target.
    void Render(const Texture2D* front, const Texture2D* back, Color edgeFrom, Color edgeTo,
                float flip);
    const Texture2D& Texture() const { return rt_.texture; }
    bool Ready() const { return rt_.id != 0; }
    // Fraction of the target the face-on cover fills; the caller scales the
    // target up by 1/kCoverFrac so the cover maps to the art rect while the
    // corners have room to swing out beyond it during the turn.
    static constexpr float kCoverFrac = 0.74f;
    void Unload();

private:
    void Ensure();

    RenderTexture2D rt_{};
    Shader shader_{};
    Texture2D patina_{};
    int locTurn_ = -1;
    int locFacing_ = -1;
    int locGloss_ = -1;
};
