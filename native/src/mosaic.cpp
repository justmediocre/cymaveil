#include "mosaic.h"

#include <algorithm>
#include <cmath>

#include "raymath.h"
#include "rlgl.h"

#include "app.h"
#include "library.h"
#include "ui.h"

namespace {

constexpr float kGap = 12.0f;
constexpr float kIsoSquash = 0.574f;  // cos(55 deg), the CSS rotateX(55deg) under ortho
constexpr float kIsoAngle = -15.0f;   // CSS rotateZ(-15deg)
// The cached layer extends this far past each screen edge, so the drift can
// slide it this far before it has to be re-rendered (the drift peaks at a few
// px/s, so that is every ten seconds or so at most).
constexpr float kLayerMargin = 48.0f;
// While only drifting, recompose once the layer has slid this far since the
// last composition. The drift is well under a pixel per frame, so composing
// every frame spends a full-screen pass plus a blur on invisible motion.
constexpr float kRedrawStep = 0.25f;

float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }  // ease-in-out

float Rand01() { return static_cast<float>(GetRandomValue(0, 100000)) / 100000.0f; }

// Cover-crop: largest centered square of the texture.
Rectangle CoverSrc(const Texture2D& tex) {
    const float side = static_cast<float>(std::min(tex.width, tex.height));
    return Rectangle{(tex.width - side) / 2.0f, (tex.height - side) / 2.0f, side, side};
}

// Tiles carry the web's 8px border-radius (at the tile's own scale — the
// grid is 2.6x the screen wide, so keep the radius proportional).
float TileRadius(Rectangle rc) { return rc.width * (8.0f / 150.0f); }

void DrawFace(const Texture2D* tex, Rectangle rc, float alpha) {
    if (alpha <= 0.003f) return;
    if (tex == nullptr) {
        // Art not resident yet: hold the slot so the grid doesn't flicker
        ui::RoundedRect(rc, TileRadius(rc), Fade(WHITE, alpha * 0.15f));
        return;
    }
    ui::RoundedTexture(*tex, CoverSrc(*tex), rc, TileRadius(rc), Fade(WHITE, alpha));
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

void Mosaic::Rebuild(const std::vector<const Art*>& arts, const MosaicSettings& s) {
    artIds_.clear();
    for (const Art* a : arts) {
        if (a != nullptr && a->Valid()) artIds_.push_back(a->path);
    }
    columns_ = std::max(2, s.density);
    rows_ = static_cast<int>(std::ceil(columns_ * 1.5f));
    tiles_.assign(static_cast<size_t>(columns_) * rows_, Tile{});
    layerDirty_ = true;
    if (artIds_.empty()) return;
    for (auto& tile : tiles_) {
        tile.front = GetRandomValue(0, static_cast<int>(artIds_.size()) - 1);
    }
}

bool Mosaic::Animating() const {
    // Something on screen moves: the grid drifts continuously while playing,
    // and swaps finish even when paused. Drives frame pacing; whether the
    // backdrop needs re-capturing is Prepare()'s call.
    if (drifting_) return true;
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
    layerDirty_ = true;  // the tile leaves the layer and is drawn live
}

void Mosaic::Update(float dt, bool playing, const MosaicSettings& s) {
    drifting_ = false;
    if (!s.enabled || tiles_.empty()) return;

    // In-flight animations always finish; drift and new swaps pause with playback.
    for (auto& tile : tiles_) {
        if (!tile.active) continue;
        tile.t += dt / Duration(tile.tr);
        if (tile.t >= 1.0f) {
            tile.front = tile.back;
            tile.t = 0;
            tile.active = false;
            layerDirty_ = true;  // settled again: back into the layer
        }
    }
    if (!playing) return;
    drifting_ = true;
    driftT_ += dt;
    nextAnim_ -= dt;
    if (nextAnim_ <= 0) {
        Trigger(s);
        nextAnim_ = 2.5f + Rand01() * 2.5f;
    }
}

const Texture2D* Mosaic::Tex(int artIdx, ArtCache& art) const {
    if (artIdx < 0 || artIdx >= static_cast<int>(artIds_.size())) return nullptr;
    return art.Get(artIds_[artIdx]);
}

void Mosaic::DrawTile(const Tile& tile, Rectangle rc, ArtCache& art, float opacity) const {
    const Texture2D* front = Tex(tile.front, art);
    if (!tile.active) {
        DrawFace(front, rc, opacity);
        return;
    }
    const Texture2D* back = Tex(tile.back, art);
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

Mosaic::Geometry Mosaic::Layout(Rectangle screen, const MosaicSettings& s) const {
    Geometry g;
    const float W = screen.width, H = screen.height;
    g.flat = s.flat;
    g.gridW = W * (s.flat ? 1.7f : 2.6f);
    g.tileSz = (g.gridW - (columns_ - 1) * kGap) / columns_;
    g.step = g.tileSz + kGap;
    g.gridH = rows_ * g.step - kGap;
    // CSS iso wrapper insets (-75% top, -45% bottom) put the plane center high
    g.center = Vector2{screen.x + W / 2, screen.y + (s.flat ? H / 2 : H * 0.35f)};
    const float rad = kIsoAngle * DEG2RAD;
    g.cosA = std::cos(rad);
    g.sinA = std::sin(rad);
    g.squash = s.flat ? 1.0f : kIsoSquash;
    return g;
}

Vector2 Mosaic::Drift(const Geometry& g) const {
    return Vector2{g.gridW * 0.022f * std::sin(driftT_ * 2 * PI / 120.0f),
                   g.gridH * 0.014f * std::sin(driftT_ * 2 * PI / 97.0f + 1.3f)};
}

Vector2 Mosaic::PlaneToScreen(const Geometry& g, Vector2 d) {
    if (g.flat) return d;
    return Vector2{d.x * g.cosA - d.y * g.sinA, g.squash * (d.x * g.sinA + d.y * g.cosA)};
}

void Mosaic::DrawTiles(const Geometry& g, Vector2 drift, Rectangle bounds, ArtCache& art, float opacity,
                       bool active) const {
    const float cullMargin = g.tileSz * 1.6f;
    rlPushMatrix();
    rlTranslatef(g.center.x, g.center.y, 0);
    rlScalef(1.0f, g.squash, 1.0f);
    if (!g.flat) rlRotatef(kIsoAngle, 0, 0, 1);
    rlTranslatef(drift.x, drift.y, 0);
    for (int row = 0; row < rows_; row++) {
        for (int col = 0; col < columns_; col++) {
            const Tile& tile = tiles_[static_cast<size_t>(row) * columns_ + col];
            if (tile.active != active) continue;
            const Rectangle rc{col * g.step - g.gridW / 2, row * g.step - g.gridH / 2, g.tileSz, g.tileSz};
            // Cull against the bounds using the same transform applied manually
            const Vector2 c = PlaneToScreen(g, Vector2{rc.x + g.tileSz / 2 + drift.x, rc.y + g.tileSz / 2 + drift.y});
            const float sx = g.center.x + c.x, sy = g.center.y + c.y;
            if (sx < bounds.x - cullMargin || sx > bounds.x + bounds.width + cullMargin ||
                sy < bounds.y - cullMargin || sy > bounds.y + bounds.height + cullMargin) {
                continue;
            }
            DrawTile(tile, rc, art, opacity);
        }
    }
    rlPopMatrix();
}

Mosaic::Change Mosaic::Prepare(Rectangle screen, ArtCache& art, const MosaicSettings& s, Color bg) {
    Change ch;
    if (!s.enabled || tiles_.empty() || artIds_.empty()) {
        if (layer_.id != 0) UnloadRenderTexture(layer_);
        layer_ = RenderTexture2D{};
        layerDirty_ = true;
        return ch;
    }
    const int lw = static_cast<int>(std::ceil(screen.width + 2 * kLayerMargin));
    const int lh = static_cast<int>(std::ceil(screen.height + 2 * kLayerMargin));
    if (layer_.id == 0 || layer_.texture.width != lw || layer_.texture.height != lh) {
        if (layer_.id != 0) UnloadRenderTexture(layer_);
        layer_ = LoadRenderTexture(lw, lh);
        SetTextureFilter(layer_.texture, TEXTURE_FILTER_BILINEAR);
        layerDirty_ = true;
    }

    const Geometry g = Layout(screen, s);
    const Vector2 drift = Drift(g);
    const Vector2 shift = PlaneToScreen(g, Vector2{drift.x - layerDrift_.x, drift.y - layerDrift_.y});
    const bool bgChanged = bg.r != layerBg_.r || bg.g != layerBg_.g || bg.b != layerBg_.b || bg.a != layerBg_.a;
    if (std::fabs(shift.x) > kLayerMargin - 1 || std::fabs(shift.y) > kLayerMargin - 1 ||
        s.opacity != layerOpacity_ || s.flat != layerFlat_ || bgChanged) {
        layerDirty_ = true;
    }

    if (layerDirty_) {
        BeginTextureMode(layer_);
        ClearBackground(bg);
        // Same separate alpha blend as the backdrop capture (see
        // Backdrop::BeginScene): translucent tiles must leave the layer opaque,
        // so it covers the scene exactly when it is drawn there.
        rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA,
                                  RL_FUNC_ADD, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM_SEPARATE);
        rlPushMatrix();
        rlTranslatef(kLayerMargin - screen.x, kLayerMargin - screen.y, 0);
        DrawTiles(g, drift, Rectangle{screen.x - kLayerMargin, screen.y - kLayerMargin, screen.width + 2 * kLayerMargin,
                                      screen.height + 2 * kLayerMargin},
                  art, s.opacity, false);
        rlPopMatrix();
        EndBlendMode();
        EndTextureMode();
        layerDrift_ = drift;
        layerOpacity_ = s.opacity;
        layerFlat_ = s.flat;
        layerBg_ = bg;
        layerDirty_ = false;
        ch.full = true;
        return ch;
    }
    if (std::fabs(shift.x - drawnShift_.x) >= kRedrawStep || std::fabs(shift.y - drawnShift_.y) >= kRedrawStep) {
        ch.full = true;
        return ch;
    }

    // Only swaps in flight: refresh the screen bounds of their tiles, placed
    // at the drift the rest of the capture was composed at. A transition
    // never draws outside its tile's rectangle.
    const Rectangle bounds = screen;
    bool any = false;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (int row = 0; row < rows_; row++) {
        for (int col = 0; col < columns_; col++) {
            if (!tiles_[static_cast<size_t>(row) * columns_ + col].active) continue;
            const float px = col * g.step - g.gridW / 2 + drawnDrift_.x;
            const float py = row * g.step - g.gridH / 2 + drawnDrift_.y;
            float tx0 = 1e9f, ty0 = 1e9f, tx1 = -1e9f, ty1 = -1e9f;
            for (int k = 0; k < 4; k++) {
                const Vector2 c = PlaneToScreen(g, Vector2{px + (k & 1) * g.tileSz, py + (k >> 1) * g.tileSz});
                tx0 = std::min(tx0, g.center.x + c.x);
                tx1 = std::max(tx1, g.center.x + c.x);
                ty0 = std::min(ty0, g.center.y + c.y);
                ty1 = std::max(ty1, g.center.y + c.y);
            }
            if (tx1 < bounds.x || ty1 < bounds.y || tx0 > bounds.x + bounds.width || ty0 > bounds.y + bounds.height) {
                continue;  // off screen: nothing visible changes
            }
            x0 = any ? std::min(x0, tx0) : tx0;
            y0 = any ? std::min(y0, ty0) : ty0;
            x1 = any ? std::max(x1, tx1) : tx1;
            y1 = any ? std::max(y1, ty1) : ty1;
            any = true;
        }
    }
    if (any) {
        // Pad for anti-aliased edges, and snap out to whole pixels.
        x0 = std::floor(std::max(bounds.x, x0 - 2));
        y0 = std::floor(std::max(bounds.y, y0 - 2));
        x1 = std::ceil(std::min(bounds.x + bounds.width, x1 + 2));
        y1 = std::ceil(std::min(bounds.y + bounds.height, y1 + 2));
        ch.partial = true;
        ch.region = Rectangle{x0, y0, x1 - x0, y1 - y0};
    }
    return ch;
}

void Mosaic::Draw(Rectangle screen, ArtCache& art, const MosaicSettings& s, Color bg, bool partial) {
    if (!s.enabled || tiles_.empty() || artIds_.empty() || layer_.id == 0) return;

    const float W = screen.width, H = screen.height;
    const Geometry g = Layout(screen, s);
    if (!partial) {
        drawnDrift_ = Drift(g);
        drawnShift_ = PlaneToScreen(g, Vector2{drawnDrift_.x - layerDrift_.x, drawnDrift_.y - layerDrift_.y});
    }
    const float lw = static_cast<float>(layer_.texture.width);
    const float lh = static_cast<float>(layer_.texture.height);
    // The layer is opaque and replaces whatever is under it; blending it
    // would only cost a full-screen read of the target.
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    DrawTexturePro(layer_.texture, Rectangle{0, 0, lw, -lh},
                   Rectangle{screen.x - kLayerMargin + drawnShift_.x, screen.y - kLayerMargin + drawnShift_.y, lw, lh},
                   Vector2{0, 0}, 0, WHITE);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
    DrawTiles(g, drawnDrift_, screen, art, s.opacity, true);

    // Gentle vignette: the mosaic stays visible across most of the screen and
    // only dims toward the farthest corners, so it still reads through the
    // frosted chrome at the edges. Transparent center out to 50% of the way to
    // the farthest corner, reaching opaque bg only past the corner (1.25).
    // Cached as a texture, stretched into the farthest-corner ellipse.
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
                const float t = Clamp((r - 0.5f) / 0.75f, 0.0f, 1.0f);
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
    if (layer_.id != 0) UnloadRenderTexture(layer_);
    layer_ = RenderTexture2D{};
    layerDirty_ = true;
    if (vignette_.id != 0) UnloadTexture(vignette_);
    vignette_ = Texture2D{};
}
