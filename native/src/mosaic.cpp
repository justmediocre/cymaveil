#include "mosaic.h"

#include <algorithm>
#include <cmath>

#include "raymath.h"
#include "rlgl.h"

#include "app.h"
#include "library.h"

namespace {

constexpr float kGap = 12.0f;
constexpr float kIsoSquash = 0.574f;  // cos(55 deg), the CSS rotateX(55deg) under ortho
constexpr float kIsoAngle = -15.0f;   // CSS rotateZ(-15deg)

float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }  // ease-in-out

float Rand01() { return static_cast<float>(GetRandomValue(0, 100000)) / 100000.0f; }

// Cover-crop: largest centered square of the texture.
Rectangle CoverSrc(const Texture2D& tex) {
    const float side = static_cast<float>(std::min(tex.width, tex.height));
    return Rectangle{(tex.width - side) / 2.0f, (tex.height - side) / 2.0f, side, side};
}

void DrawFace(const Texture2D* tex, Rectangle rc, float alpha) {
    if (alpha <= 0.003f) return;
    if (tex == nullptr) {
        // Art not resident yet: hold the slot so the grid doesn't flicker
        DrawRectangleRec(rc, Fade(WHITE, alpha * 0.15f));
        return;
    }
    DrawTexturePro(*tex, CoverSrc(*tex), rc, Vector2{0, 0}, 0, Fade(WHITE, alpha));
}

// Expanding circular reveal, vertices clamped to the tile rect so the circle
// morphs into the square as it grows (replaces CSS clip-path: circle()).
void DrawIrisFace(const Texture2D* tex, Rectangle rc, float frac, float alpha) {
    if (tex == nullptr || frac <= 0.0f || alpha <= 0.003f) return;
    const Rectangle src = CoverSrc(*tex);
    const Vector2 c{rc.x + rc.width / 2, rc.y + rc.height / 2};
    const float maxR = 0.75f * std::sqrt(rc.width * rc.width + rc.height * rc.height);
    const float r = frac * maxR;
    constexpr int kSegments = 28;
    const auto uv = [&](Vector2 p) {
        return Vector2{(src.x + (p.x - rc.x) / rc.width * src.width) / tex->width,
                       (src.y + (p.y - rc.y) / rc.height * src.height) / tex->height};
    };
    const auto clamped = [&](float ang) {
        return Vector2{Clamp(c.x + r * std::cos(ang), rc.x, rc.x + rc.width),
                       Clamp(c.y + r * std::sin(ang), rc.y, rc.y + rc.height)};
    };
    const unsigned char a = static_cast<unsigned char>(alpha * 255);
    rlDisableBackfaceCulling();  // fan winding varies; draw both sides
    rlSetTexture(tex->id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, a);
    const Vector2 cuv = uv(c);
    for (int i = 0; i < kSegments; i++) {
        const Vector2 v0 = clamped(2 * PI * i / kSegments);
        const Vector2 v1 = clamped(2 * PI * (i + 1) / kSegments);
        const Vector2 t0 = uv(v0), t1 = uv(v1);
        rlTexCoord2f(cuv.x, cuv.y);
        rlVertex2f(c.x, c.y);
        rlTexCoord2f(t0.x, t0.y);
        rlVertex2f(v0.x, v0.y);
        rlTexCoord2f(t1.x, t1.y);
        rlVertex2f(v1.x, v1.y);
        rlTexCoord2f(t1.x, t1.y);
        rlVertex2f(v1.x, v1.y);
    }
    rlEnd();
    rlSetTexture(0);
    rlEnableBackfaceCulling();
}

Rectangle ScaledRect(Rectangle rc, float sx, float sy) {
    return Rectangle{rc.x + rc.width * (1 - sx) / 2, rc.y + rc.height * (1 - sy) / 2,
                     rc.width * sx, rc.height * sy};
}

// Center-weighted pick ported from weightedCenterPick(): bias toward the
// vignette's transparent center at (0.5, 0.6).
int WeightedCenterPick(const std::vector<int>& indices, int columns, int rows) {
    std::vector<float> weights(indices.size());
    float total = 0;
    for (size_t j = 0; j < indices.size(); j++) {
        const int i = indices[j];
        const float nx = columns > 1 ? static_cast<float>(i % columns) / (columns - 1) : 0.5f;
        const float ny = rows > 1 ? static_cast<float>(i / columns) / (rows - 1) : 0.5f;
        const float dx = nx - 0.5f, dy = ny - 0.6f;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float w = std::pow(1.0f - std::min(dist / 0.8f, 1.0f), 2.0f);
        weights[j] = w;
        total += w;
    }
    if (total <= 0) return indices[GetRandomValue(0, static_cast<int>(indices.size()) - 1)];
    float r = Rand01() * total;
    for (size_t j = 0; j < indices.size(); j++) {
        r -= weights[j];
        if (r <= 0) return indices[j];
    }
    return indices.back();
}

}  // namespace

float Mosaic::Duration(Tr tr) {
    switch (tr) {
        case Tr::Flip: return 0.8f;
        case Tr::ShrinkGrow: return 0.9f;
        case Tr::CrossFade: return 1.0f;
        case Tr::Fade: return 1.0f;
        case Tr::Iris: return 1.2f;
    }
    return 1.0f;
}

void Mosaic::Rebuild(const std::vector<Album>& albums, const MosaicSettings& s) {
    artIds_.clear();
    for (const auto& a : albums) {
        if (!a.artPath.empty()) artIds_.push_back(a.id);
    }
    columns_ = std::max(2, s.density);
    rows_ = static_cast<int>(std::ceil(columns_ * 1.5f));
    tiles_.assign(static_cast<size_t>(columns_) * rows_, Tile{});
    if (artIds_.empty()) return;
    for (auto& tile : tiles_) {
        tile.front = GetRandomValue(0, static_cast<int>(artIds_.size()) - 1);
    }
}

bool Mosaic::Animating() const {
    return std::any_of(tiles_.begin(), tiles_.end(), [](const Tile& t) { return t.active; });
}

void Mosaic::Trigger(const MosaicSettings& s) {
    if (artIds_.size() < 2 || tiles_.empty()) return;
    std::vector<int> available;
    for (size_t i = 0; i < tiles_.size(); i++) {
        if (!tiles_[i].active) available.push_back(static_cast<int>(i));
    }
    if (available.empty()) return;
    Tile& tile = tiles_[WeightedCenterPick(available, columns_, rows_)];
    do {
        tile.back = GetRandomValue(0, static_cast<int>(artIds_.size()) - 1);
    } while (tile.back == tile.front);
    if (s.transition == "flip") tile.tr = Tr::Flip;
    else if (s.transition == "shrink-grow") tile.tr = Tr::ShrinkGrow;
    else if (s.transition == "cross-fade") tile.tr = Tr::CrossFade;
    else if (s.transition == "fade") tile.tr = Tr::Fade;
    else if (s.transition == "iris") tile.tr = Tr::Iris;
    else tile.tr = static_cast<Tr>(GetRandomValue(0, 4));
    tile.t = 0;
    tile.active = true;
}

void Mosaic::Update(float dt, bool playing, const MosaicSettings& s) {
    if (!s.enabled || tiles_.empty()) return;

    // In-flight animations always finish; drift and new swaps pause with playback.
    for (auto& tile : tiles_) {
        if (!tile.active) continue;
        tile.t += dt / Duration(tile.tr);
        if (tile.t >= 1.0f) {
            tile.front = tile.back;
            tile.t = 0;
            tile.active = false;
        }
    }
    if (!playing) return;
    driftT_ += dt;
    nextAnim_ -= dt;
    if (nextAnim_ <= 0) {
        Trigger(s);
        nextAnim_ = 2.5f + Rand01() * 2.5f;
    }
}

const Texture2D* Mosaic::Tex(int artIdx, ArtCache& art, const Library& lib) const {
    if (artIdx < 0 || artIdx >= static_cast<int>(artIds_.size())) return nullptr;
    const Album* album = lib.AlbumById(artIds_[artIdx]);
    return album != nullptr ? art.Get(*album) : nullptr;
}

void Mosaic::DrawTile(const Tile& tile, Rectangle rc, ArtCache& art, const Library& lib,
                      float opacity) const {
    const Texture2D* front = Tex(tile.front, art, lib);
    if (!tile.active) {
        DrawFace(front, rc, opacity);
        return;
    }
    const Texture2D* back = Tex(tile.back, art, lib);
    const float e = Smooth(Clamp(tile.t, 0.0f, 1.0f));
    switch (tile.tr) {
        case Tr::Flip: {
            // Fake Y-rotation: width squash with a face swap at 90 degrees
            const float w = std::fabs(std::cos(e * PI));
            DrawFace(e < 0.5f ? front : back, ScaledRect(rc, w, 1.0f), opacity);
            break;
        }
        case Tr::ShrinkGrow: {
            const float sc = std::fabs(1.0f - 2.0f * e);
            DrawFace(e < 0.5f ? front : back, ScaledRect(rc, sc, sc), opacity);
            break;
        }
        case Tr::CrossFade:
            DrawFace(front, rc, opacity * (1.0f - e));
            DrawFace(back, rc, opacity * e);
            break;
        case Tr::Fade:
            DrawFace(e < 0.5f ? front : back, rc, opacity * std::fabs(1.0f - 2.0f * e));
            break;
        case Tr::Iris:
            DrawFace(front, rc, opacity);
            DrawIrisFace(back, rc, e, opacity);
            break;
    }
}

void Mosaic::Draw(Rectangle screen, ArtCache& art, const Library& lib, const MosaicSettings& s,
                  Color bg) {
    if (!s.enabled || tiles_.empty() || artIds_.empty()) return;

    const float W = screen.width, H = screen.height;
    const float gridW = W * (s.flat ? 1.4f : 2.2f);
    const float tileSz = (gridW - (columns_ - 1) * kGap) / columns_;
    const float step = tileSz + kGap;
    const float gridH = rows_ * step - kGap;
    // CSS iso wrapper insets (-75% top, -45% bottom) put the plane center high
    const Vector2 center{screen.x + W / 2, screen.y + (s.flat ? H / 2 : H * 0.35f)};
    const Vector2 drift{gridW * 0.022f * std::sin(driftT_ * 2 * PI / 120.0f),
                        gridH * 0.014f * std::sin(driftT_ * 2 * PI / 97.0f + 1.3f)};
    const float rad = kIsoAngle * DEG2RAD;
    const float cosA = std::cos(rad), sinA = std::sin(rad);
    const float squash = s.flat ? 1.0f : kIsoSquash;
    const float cullMargin = tileSz * 1.6f;

    rlPushMatrix();
    rlTranslatef(center.x, center.y, 0);
    rlScalef(1.0f, squash, 1.0f);
    if (!s.flat) rlRotatef(kIsoAngle, 0, 0, 1);
    rlTranslatef(drift.x, drift.y, 0);

    for (int row = 0; row < rows_; row++) {
        for (int col = 0; col < columns_; col++) {
            const Rectangle rc{col * step - gridW / 2, row * step - gridH / 2, tileSz, tileSz};
            // Cull against the screen using the same transform applied manually
            const float gx = rc.x + tileSz / 2 + drift.x;
            const float gy = rc.y + tileSz / 2 + drift.y;
            const float sx = center.x + (s.flat ? gx : gx * cosA - gy * sinA);
            const float sy = center.y + squash * (s.flat ? gy : gx * sinA + gy * cosA);
            if (sx < screen.x - cullMargin || sx > screen.x + W + cullMargin ||
                sy < screen.y - cullMargin || sy > screen.y + H + cullMargin) {
                continue;
            }
            DrawTile(tiles_[static_cast<size_t>(row) * columns_ + col], rc, art, lib, s.opacity);
        }
    }
    rlPopMatrix();

    // Vignette matching the CSS radial-gradient(ellipse at 50% 60%,
    // transparent 10%, bg 70%): transparent center, fully opaque background by
    // 70% of the way to the farthest corner. Cached as a texture, stretched
    // into the farthest-corner ellipse.
    const int vw = 256, vh = 256;
    // Rebake when the theme changes: bg is baked into the texture, so a stale
    // dark vignette would keep fading to black under the light palette.
    const bool bgChanged = bg.r != vignetteBg_.r || bg.g != vignetteBg_.g ||
                           bg.b != vignetteBg_.b || bg.a != vignetteBg_.a;
    if (vignette_.id != 0 && bgChanged) {
        UnloadTexture(vignette_);
        vignette_ = Texture2D{};
    }
    if (vignette_.id == 0) {
        vignetteBg_ = bg;
        Image img = GenImageColor(vw, vh, BLANK);
        auto* px = static_cast<Color*>(img.data);
        for (int y = 0; y < vh; y++) {
            for (int x = 0; x < vw; x++) {
                const float dx = (x - vw / 2.0f) / (vw / 2.0f);
                const float dy = (y - vh / 2.0f) / (vh / 2.0f);
                const float r = std::sqrt(dx * dx + dy * dy);
                const float t = Clamp((r - 0.1f) / 0.6f, 0.0f, 1.0f);
                px[y * vw + x] = Fade(bg, t);
            }
        }
        vignette_ = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(vignette_, TEXTURE_FILTER_BILINEAR);
    }
    // farthest-corner ellipse semi-axes for a center at (0.5, 0.6)
    const float rx = std::sqrt(2.0f) * 0.5f * W;
    const float ry = std::sqrt(2.0f) * 0.6f * H;
    const Vector2 vc{screen.x + W / 2, screen.y + H * 0.6f};
    DrawTexturePro(vignette_, Rectangle{0, 0, static_cast<float>(vw), static_cast<float>(vh)},
                   Rectangle{vc.x - rx, vc.y - ry, rx * 2, ry * 2}, Vector2{0, 0}, 0, WHITE);
    // The ellipse doesn't reach the screen's top corners; flood what's left
    if (vc.y - ry > screen.y) {
        DrawRectangleRec(Rectangle{screen.x, screen.y, W, vc.y - ry - screen.y}, bg);
    }
}

void Mosaic::Unload() {
    if (vignette_.id != 0) UnloadTexture(vignette_);
    vignette_ = Texture2D{};
}
