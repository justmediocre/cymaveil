#include "backdrop.h"

#include <algorithm>
#include <cmath>

#include "rlgl.h"

#include "ui.h"

namespace {

// The blur runs at 1/kDownscale resolution: bilinear downsampling already
// softens the image, so a modest kernel reads as a heavy frosted blur while
// staying cheap. kSpread widens the tap spacing for an even softer result.
constexpr int kDownscale = 4;
constexpr float kSpread = 2.0f;

// Separable 9-tap Gaussian. `direction` selects the axis (and folds in the
// spread); `resolution` is the size of the sampled texture so the taps land on
// pixel centers. Desktop GL: #version 330. Alpha is forced to 1 so each pass
// fully overwrites its (never cleared) target: with a translucent result the
// alpha blend would keep a fraction of whatever the target held before — the
// previous capture, or uninitialised memory after a resize — and the glass
// would show the old mosaic ghosting through the new one.
const char* kBlurFs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 resolution;
uniform vec2 direction;
out vec4 finalColor;
void main() {
    vec2 texel = direction / resolution;
    vec4 c = texture(texture0, fragTexCoord) * 0.227027;
    c += texture(texture0, fragTexCoord + texel * 1.0) * 0.1945946;
    c += texture(texture0, fragTexCoord - texel * 1.0) * 0.1945946;
    c += texture(texture0, fragTexCoord + texel * 2.0) * 0.1216216;
    c += texture(texture0, fragTexCoord - texel * 2.0) * 0.1216216;
    c += texture(texture0, fragTexCoord + texel * 3.0) * 0.054054;
    c += texture(texture0, fragTexCoord - texel * 3.0) * 0.054054;
    c += texture(texture0, fragTexCoord + texel * 4.0) * 0.016216;
    c += texture(texture0, fragTexCoord - texel * 4.0) * 0.016216;
    finalColor = vec4(c.rgb, 1.0);
}
)";

// Source rect that samples a render texture's (vertically flipped) contents for
// the screen-space rectangle r. Mirrors the canonical {0,0,W,-H} fullscreen
// flip, generalized to a sub-rect.
Rectangle FlipSrc(Rectangle r, float texW, float texH, float screenW, float screenH) {
    const float sx = texW / screenW, sy = texH / screenH;
    return Rectangle{r.x * sx, texH - (r.y + r.height) * sy, r.width * sx, -(r.height * sy)};
}

}  // namespace

void Backdrop::EnsureSize(int w, int h) {
    if (w == w_ && h == h_ && ready_) return;
    Unload();
    dirty_ = true;  // fresh targets hold no image yet; force a capture
    w_ = w;
    h_ = h;
    if (w <= 0 || h <= 0) return;
    const int bw = std::max(1, w / kDownscale);
    const int bh = std::max(1, h / kDownscale);
    scene_ = LoadRenderTexture(w, h);
    blurA_ = LoadRenderTexture(bw, bh);
    blur_ = LoadRenderTexture(bw, bh);
    SetTextureFilter(scene_.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(blurA_.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(blur_.texture, TEXTURE_FILTER_BILINEAR);
    // Clamp, not raylib's default repeat: the blur taps run past the edges,
    // and wrapping would bleed the bottom of the mosaic into glass at the top
    // of the window (and right into left). It also keeps a partial re-blur
    // local to the dirty region.
    SetTextureWrap(scene_.texture, TEXTURE_WRAP_CLAMP);
    SetTextureWrap(blurA_.texture, TEXTURE_WRAP_CLAMP);
    SetTextureWrap(blur_.texture, TEXTURE_WRAP_CLAMP);
    if (shader_.id == 0) {
        shader_ = LoadShaderFromMemory(nullptr, kBlurFs);
        resLoc_ = GetShaderLocation(shader_, "resolution");
        dirLoc_ = GetShaderLocation(shader_, "direction");
    }
    ready_ = true;
}

void Backdrop::MarkDirtyRegion(Rectangle r) {
    if (r.width <= 0 || r.height <= 0) return;
    if (!hasRegion_) {
        region_ = r;
        hasRegion_ = true;
        return;
    }
    const float x0 = std::min(region_.x, r.x), y0 = std::min(region_.y, r.y);
    const float x1 = std::max(region_.x + region_.width, r.x + r.width);
    const float y1 = std::max(region_.y + region_.height, r.y + r.height);
    region_ = Rectangle{x0, y0, x1 - x0, y1 - y0};
}

void Backdrop::BeginScene() {
    if (!ready_) return;
    BeginTextureMode(scene_);
    if (PartialRender()) {
        BeginScissorMode(static_cast<int>(region_.x), static_cast<int>(region_.y),
                         static_cast<int>(region_.width), static_cast<int>(region_.height));
    }
    // Translucent draws (tiles at mosaic opacity, the vignette) must not eat
    // into the target's alpha: blend colour normally but keep alpha at
    // dst + src·(1-dst), so the capture stays opaque. With the default
    // (src·a + dst·(1-a) on alpha too) a tile at opacity a leaves alpha
    // 1-a+a², and everything that later draws scene_/blur_ alpha-blends
    // instead of covering: the sharp mosaic bleeds through the frost and the
    // blur passes keep part of the previous capture.
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA,
                              RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
}

void Backdrop::EndScene() {
    if (!ready_) return;
    const bool partial = PartialRender();
    EndBlendMode();
    if (partial) EndScissorMode();
    EndTextureMode();
    Blur(partial);
    dirty_ = false;  // scene_/blur_ now match the latest mosaic
    hasRegion_ = false;
}

void Backdrop::Blur(bool partial) {
    const float fw = static_cast<float>(w_), fh = static_cast<float>(h_);
    const float bw = static_cast<float>(blur_.texture.width);
    const float bh = static_cast<float>(blur_.texture.height);
    const Rectangle bdst{0, 0, bw, bh};

    // A partial capture only changed region_, so only blurred pixels within
    // the kernel's reach of it can differ. In blur-texture pixels: the
    // horizontal pass reads kSpread*4 scene px either side and a little
    // vertically for the downsample; the vertical pass reads kSpread*4 of
    // its own texels up and down. Pad a little beyond that.
    int ax = 0, ay = 0, aw = 0, ah = 0, bx = 0, by = 0, bwi = 0, bhi = 0;
    if (partial) {
        const int bwMax = blur_.texture.width, bhMax = blur_.texture.height;
        const int reachH = static_cast<int>(std::ceil(kSpread * 4 / kDownscale)) + 2;
        const int reachV = static_cast<int>(std::ceil(kSpread * 4)) + 2;
        const int x0 = static_cast<int>(std::floor(region_.x / kDownscale)) - reachH;
        const int y0 = static_cast<int>(std::floor(region_.y / kDownscale)) - 2;
        const int x1 = static_cast<int>(std::ceil((region_.x + region_.width) / kDownscale)) + reachH;
        const int y1 = static_cast<int>(std::ceil((region_.y + region_.height) / kDownscale)) + 2;
        ax = std::max(0, x0);
        ay = std::max(0, y0);
        aw = std::min(bwMax, x1) - ax;
        ah = std::min(bhMax, y1) - ay;
        bx = std::max(0, x0 - 1);
        by = std::max(0, y0 - reachV);
        bwi = std::min(bwMax, x1 + 1) - bx;
        bhi = std::min(bhMax, y1 + reachV) - by;
        if (aw <= 0 || ah <= 0 || bwi <= 0 || bhi <= 0) return;
    }

    BeginShaderMode(shader_);
    // Horizontal pass: downsample the full-res scene into blurA_.
    const Vector2 sceneRes{fw, fh};
    const Vector2 horiz{kSpread, 0.0f};
    SetShaderValue(shader_, resLoc_, &sceneRes, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader_, dirLoc_, &horiz, SHADER_UNIFORM_VEC2);
    BeginTextureMode(blurA_);
    if (partial) BeginScissorMode(ax, ay, aw, ah);
    DrawTexturePro(scene_.texture, Rectangle{0, 0, fw, -fh}, bdst, Vector2{0, 0}, 0, WHITE);
    if (partial) EndScissorMode();
    EndTextureMode();

    // Vertical pass: blurA_ -> blur_, both at the downsampled resolution.
    const Vector2 blurRes{bw, bh};
    const Vector2 vert{0.0f, kSpread};
    SetShaderValue(shader_, resLoc_, &blurRes, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader_, dirLoc_, &vert, SHADER_UNIFORM_VEC2);
    BeginTextureMode(blur_);
    if (partial) BeginScissorMode(bx, by, bwi, bhi);
    DrawTexturePro(blurA_.texture, Rectangle{0, 0, bw, -bh}, bdst, Vector2{0, 0}, 0, WHITE);
    if (partial) EndScissorMode();
    EndTextureMode();
    EndShaderMode();
}

void Backdrop::DrawScene() const {
    if (!ready_) return;
    const float fw = static_cast<float>(w_), fh = static_cast<float>(h_);
    DrawTexturePro(scene_.texture, Rectangle{0, 0, fw, -fh}, Rectangle{0, 0, fw, fh},
                   Vector2{0, 0}, 0, WHITE);
}

void Backdrop::DrawGlass(Rectangle r, Color tint, bool blur) const {
    if (!ready_ || !blur) {
        DrawRectangleRec(r, tint);
        return;
    }
    const float tw = static_cast<float>(blur_.texture.width);
    const float th = static_cast<float>(blur_.texture.height);
    const Rectangle src = FlipSrc(r, tw, th, static_cast<float>(w_), static_cast<float>(h_));
    DrawTexturePro(blur_.texture, src, r, Vector2{0, 0}, 0, WHITE);
    DrawRectangleRec(r, tint);
}

void Backdrop::DrawGlassRounded(Rectangle r, Color tint, float radius, bool blur) const {
    if (ready_ && blur) {
        const float tw = static_cast<float>(blur_.texture.width);
        const float th = static_cast<float>(blur_.texture.height);
        const Rectangle src = FlipSrc(r, tw, th, static_cast<float>(w_), static_cast<float>(h_));
        ui::RoundedTexture(blur_.texture, src, r, radius, WHITE);
    }
    ui::RoundedRect(r, radius, tint);
}

void Backdrop::Unload() {
    if (scene_.id != 0) UnloadRenderTexture(scene_);
    if (blurA_.id != 0) UnloadRenderTexture(blurA_);
    if (blur_.id != 0) UnloadRenderTexture(blur_);
    if (shader_.id != 0) UnloadShader(shader_);
    scene_ = RenderTexture2D{};
    blurA_ = RenderTexture2D{};
    blur_ = RenderTexture2D{};
    shader_ = Shader{};
    ready_ = false;
}
