#pragma once

#include <string>

#include "raylib.h"

// Ambilight-style bloom behind the Now Playing cover. The cover is drawn into a
// padded offscreen target and gaussian-blurred, so its edge colors bleed into
// the margin and fade out; drawn enlarged + additive behind the art it reads as
// soft light spilling from the cover, tinted by the art itself rather than a
// flat dominant-color wash. Rebuilt only when the shown album changes.
class AlbumGlow {
public:
    // Cheap when albumId is unchanged; rebuilds the blurred bloom otherwise.
    // Must run outside BeginDrawing (it renders to offscreen targets).
    void Update(const Texture2D& cover, const std::string& albumId);
    bool Ready() const { return blur_.id != 0 && !albumId_.empty(); }
    const Texture2D& Texture() const { return blur_.texture; }
    // Fraction of the target the cover occupies; the rest is the faded bloom
    // margin. Lets the caller scale the target so the content maps to the art.
    static constexpr float kContentFrac = 0.72f;
    void Unload();

private:
    void Ensure();

    RenderTexture2D padded_{};  // cover inset into a transparent border
    RenderTexture2D blurA_{};   // horizontal pass / ping-pong
    RenderTexture2D blur_{};    // vertical pass / final
    Shader shader_{};
    int resLoc_ = -1;
    int dirLoc_ = -1;
    std::string albumId_;
};
