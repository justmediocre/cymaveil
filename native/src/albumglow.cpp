#include "albumglow.h"

#include <algorithm>

#include "raylib.h"

namespace {

constexpr int kSize = 128;     // bloom target resolution (small; it's blurred)
constexpr float kSpread = 2.6f;  // gaussian tap spacing, in texels
constexpr int kPasses = 2;       // H+V iterations

// Separable 9-tap gaussian, same kernel as the backdrop blur. Desktop GL 330.
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
    finalColor = c;
}
)";

}  // namespace

void AlbumGlow::Ensure() {
    if (padded_.id == 0) {
        padded_ = LoadRenderTexture(kSize, kSize);
        blurA_ = LoadRenderTexture(kSize, kSize);
        blur_ = LoadRenderTexture(kSize, kSize);
        SetTextureFilter(padded_.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(blurA_.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(blur_.texture, TEXTURE_FILTER_BILINEAR);
    }
    if (shader_.id == 0) {
        shader_ = LoadShaderFromMemory(nullptr, kBlurFs);
        resLoc_ = GetShaderLocation(shader_, "resolution");
        dirLoc_ = GetShaderLocation(shader_, "direction");
    }
}

void AlbumGlow::Update(const Texture2D& cover, const std::string& albumId) {
    Ensure();
    if (albumId == albumId_ || albumId.empty()) return;
    albumId_ = albumId;

    // Center-crop the cover into the inset region; the transparent border is
    // what the blur bleeds the edge colors into to make a faded halo.
    const float n = static_cast<float>(kSize);
    const float inset = n * (1.0f - kContentFrac) / 2.0f;
    const Rectangle dst{inset, inset, n - 2 * inset, n - 2 * inset};
    const float side = static_cast<float>(std::min(cover.width, cover.height));
    const Rectangle src{(cover.width - side) / 2, (cover.height - side) / 2, side, side};
    BeginTextureMode(padded_);
    ClearBackground(BLANK);
    DrawTexturePro(cover, src, dst, Vector2{0, 0}, 0, WHITE);
    EndTextureMode();

    const Rectangle full{0, 0, n, n};
    const Rectangle flip{0, 0, n, -n};
    const Vector2 res{n, n};
    BeginShaderMode(shader_);
    SetShaderValue(shader_, resLoc_, &res, SHADER_UNIFORM_VEC2);
    RenderTexture2D srcRt = padded_;
    for (int i = 0; i < kPasses; i++) {
        const Vector2 horiz{kSpread, 0.0f};
        SetShaderValue(shader_, dirLoc_, &horiz, SHADER_UNIFORM_VEC2);
        BeginTextureMode(blurA_);
        DrawTexturePro(srcRt.texture, flip, full, Vector2{0, 0}, 0, WHITE);
        EndTextureMode();

        const Vector2 vert{0.0f, kSpread};
        SetShaderValue(shader_, dirLoc_, &vert, SHADER_UNIFORM_VEC2);
        BeginTextureMode(blur_);
        DrawTexturePro(blurA_.texture, flip, full, Vector2{0, 0}, 0, WHITE);
        EndTextureMode();
        srcRt = blur_;  // feed the next iteration from the blurred result
    }
    EndShaderMode();
}

void AlbumGlow::Unload() {
    if (padded_.id != 0) UnloadRenderTexture(padded_);
    if (blurA_.id != 0) UnloadRenderTexture(blurA_);
    if (blur_.id != 0) UnloadRenderTexture(blur_);
    if (shader_.id != 0) UnloadShader(shader_);
    padded_ = RenderTexture2D{};
    blurA_ = RenderTexture2D{};
    blur_ = RenderTexture2D{};
    shader_ = Shader{};
    albumId_.clear();
}
