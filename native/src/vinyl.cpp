#include "vinyl.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "ui.h"

namespace {

constexpr int kTexSize = 512;

float Smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct Stop {
    float pos;
    float value;
};

float InterpStops(const std::vector<Stop>& stops, float pos) {
    if (pos <= stops.front().pos) return stops.front().value;
    for (size_t i = 1; i < stops.size(); i++) {
        if (pos <= stops[i].pos) {
            const float t = (pos - stops[i - 1].pos) / (stops[i].pos - stops[i - 1].pos);
            return stops[i - 1].value + (stops[i].value - stops[i - 1].value) * t;
        }
    }
    return stops.back().value;
}

// The disc body: the CSS radial-gradient groove rings, baked once.
// Grayscale stops: #111=17, #1a1a1a=26, #282828=40, #333=51.
Texture2D GenBodyTexture() {
    std::vector<Stop> stops{{0.0f, 17}, {0.17f, 17}, {0.173f, 51}, {0.176f, 17}, {0.20f, 26}};
    for (int p = 22; p <= 94; p += 3) {
        stops.push_back({p / 100.0f, 40});
        stops.push_back({(p + 0.5f) / 100.0f, 26});
    }
    stops.push_back({0.97f, 51});
    stops.push_back({1.0f, 17});

    Image img = GenImageColor(kTexSize, kTexSize, BLANK);
    auto* px = static_cast<Color*>(img.data);
    const float R = kTexSize / 2.0f;
    for (int y = 0; y < kTexSize; y++) {
        for (int x = 0; x < kTexSize; x++) {
            const float dx = x - R + 0.5f, dy = y - R + 0.5f;
            const float r = std::sqrt(dx * dx + dy * dy) / R;
            if (r > 1.0f) continue;
            float v = InterpStops(stops, r);
            // inset 0 0 30px rgba(0,0,0,0.6) at the web's ~367px disc
            v *= 1.0f - 0.6f * Smoothstep(0.84f, 1.0f, r);
            // rim highlight: inset 0 0 1px 1px rgba(255,255,255,0.08)
            v += 255.0f * 0.08f * Smoothstep(0.985f, 1.0f, r);
            const auto g = static_cast<unsigned char>(std::clamp(v, 0.0f, 255.0f));
            const auto a = static_cast<unsigned char>(255 * std::clamp((1.0f - r) * R / 1.5f, 0.0f, 1.0f));
            px[y * kTexSize + x] = Color{g, g, g, a};
        }
    }
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&tex);
    SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
    return tex;
}

// The light-catch sheen: the CSS conic-gradient of white highlights. This is
// the rotating layer that makes the spin visible.
Texture2D GenSheenTexture() {
    const std::vector<Stop> stops{{0, 0},     {40, 0.06f},  {90, 0.12f}, {140, 0.06f}, {180, 0},
                                  {220, 0.03f}, {270, 0.08f}, {320, 0.03f}, {360, 0}};
    Image img = GenImageColor(kTexSize, kTexSize, BLANK);
    auto* px = static_cast<Color*>(img.data);
    const float R = kTexSize / 2.0f;
    for (int y = 0; y < kTexSize; y++) {
        for (int x = 0; x < kTexSize; x++) {
            const float dx = x - R + 0.5f, dy = y - R + 0.5f;
            const float r = std::sqrt(dx * dx + dy * dy) / R;
            if (r > 1.0f) continue;
            // CSS conic gradients start at 12 o'clock and run clockwise
            float ang = std::atan2(dx, -dy) * RAD2DEG;
            if (ang < 0) ang += 360.0f;
            const float a = InterpStops(stops, ang) * std::clamp((1.0f - r) * R / 1.5f, 0.0f, 1.0f);
            px[y * kTexSize + x] = Color{255, 255, 255, static_cast<unsigned char>(a * 255)};
        }
    }
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&tex);
    SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
    return tex;
}

}  // namespace

void Vinyl::EnsureTextures() {
    if (body_.id == 0) body_ = GenBodyTexture();
    if (sheen_.id == 0) sheen_ = GenSheenTexture();
}

void Vinyl::Draw(Vector2 c, float d, float spinDeg, float alpha, Color labelAccent,
                 Color labelDominant) {
    if (alpha <= 0.004f) return;
    EnsureTextures();
    const Rectangle src{0, 0, kTexSize, kTexSize};
    const Rectangle disc{c.x - d / 2, c.y - d / 2, d, d};

    // box-shadow: 0 2px 20px rgba(0,0,0,0.5)
    ui::Shadow(disc, d / 2, 0, 2, 20, 0, Fade(BLACK, 0.5f * alpha));
    DrawTexturePro(body_, src, disc, Vector2{0, 0}, 0, Fade(WHITE, alpha));
    DrawTexturePro(sheen_, src, Rectangle{c.x, c.y, d, d}, Vector2{d / 2, d / 2}, spinDeg,
                   Fade(WHITE, alpha));

    // Center label: accent -> dominant radial, 20% of the disc, spindle hole 14%
    const float labelR = d * 0.10f;
    ui::Shadow(Rectangle{c.x - labelR, c.y - labelR, labelR * 2, labelR * 2}, labelR, 0, 0, 8, 0,
               Fade(BLACK, 0.5f * alpha));
    DrawCircleGradient(c, labelR, Fade(labelAccent, alpha),
                       Fade(labelDominant, alpha));
    DrawCircleV(c, labelR * 0.14f, Fade(Color{17, 17, 17, 255}, alpha));
}

void Vinyl::Unload() {
    if (body_.id != 0) UnloadTexture(body_);
    if (sheen_.id != 0) UnloadTexture(sheen_);
    body_ = Texture2D{};
    sheen_ = Texture2D{};
}
