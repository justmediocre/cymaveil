#include "blur.h"

namespace {

// Separable 9-tap Gaussian. `direction` selects the axis (and folds in the
// spread); `resolution` is the size of the sampled texture so the taps land
// on pixel centers.
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

void GaussianBlur::Ensure() {
    if (shader_.id == 0) {
        shader_ = LoadShaderFromMemory(nullptr, kBlurFs);
        resLoc_ = GetShaderLocation(shader_, "resolution");
        dirLoc_ = GetShaderLocation(shader_, "direction");
    }
}

void GaussianBlur::Run(const Texture2D& src, Rectangle srcRect, RenderTexture2D& tmp,
                       RenderTexture2D& dst, float spread, int passes) {
    Ensure();
    const float bw = static_cast<float>(dst.texture.width);
    const float bh = static_cast<float>(dst.texture.height);
    const Rectangle full{0, 0, bw, bh};
    const Rectangle flip{0, 0, bw, -bh};
    BeginShaderMode(shader_);
    const Texture2D* cur = &src;
    Rectangle curSrc = srcRect;
    for (int i = 0; i < passes; i++) {
        const Vector2 res{static_cast<float>(cur->width), static_cast<float>(cur->height)};
        const Vector2 horiz{spread, 0.0f};
        SetShaderValue(shader_, resLoc_, &res, SHADER_UNIFORM_VEC2);
        SetShaderValue(shader_, dirLoc_, &horiz, SHADER_UNIFORM_VEC2);
        BeginTextureMode(tmp);
        DrawTexturePro(*cur, curSrc, full, Vector2{0, 0}, 0, WHITE);
        EndTextureMode();

        const Vector2 res2{bw, bh};
        const Vector2 vert{0.0f, spread};
        SetShaderValue(shader_, resLoc_, &res2, SHADER_UNIFORM_VEC2);
        SetShaderValue(shader_, dirLoc_, &vert, SHADER_UNIFORM_VEC2);
        BeginTextureMode(dst);
        DrawTexturePro(tmp.texture, flip, full, Vector2{0, 0}, 0, WHITE);
        EndTextureMode();
        cur = &dst.texture;
        curSrc = flip;
    }
    EndShaderMode();
}

void GaussianBlur::Unload() {
    if (shader_.id != 0) UnloadShader(shader_);
    shader_ = Shader{};
}
