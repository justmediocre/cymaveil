#include "vinyl.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int kTexSize = 512;
constexpr float kSpinSecondsPerRev = 1.8f;
// 65px slide at a 340px art card (web tuning). The disc sits slightly
// smaller than the artwork so it reads as a record inside its sleeve —
// it never pokes above or below the art, only out the side while playing.
constexpr float kSlideFrac = 65.0f / 340.0f;
constexpr float kDiscScale = 0.96f;

// cubic-bezier(0.22, 1, 0.36, 1) approximation
float EaseOut(float t) {
    const float inv = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - inv * inv * inv * inv;
}

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
            // inset box-shadow (30px black 0.6 at the web's 340px card)
            v *= 1.0f - 0.6f * Smoothstep(0.84f, 1.0f, r);
            const auto g = static_cast<unsigned char>(v);
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

void Vinyl::Update(float dt, const std::string& targetAlbumId, bool playing, bool skipIntent,
                   bool enabled) {
    if (skipIntent) fast_ = true;

    // A different album wants the stage: buffer it and tuck the current record
    // into its sleeve first. The art only flips over once the disc is hidden;
    // if it's already away (paused, vinyl disabled) the flip begins at once.
    if (targetAlbumId != displayed_ && targetAlbumId != pending_) {
        pending_ = targetAlbumId;
        out_ = false;
        if (slide_ <= 0.001f && !flipping_) {
            flipping_ = true;
            flip_ = 0;
        }
    } else if (!flipping_ && !pending_.empty() && targetAlbumId == displayed_) {
        // A pre-fired target backed out before the flip began (paused at the
        // brink, or the predicted next track changed): abandon the retract and
        // let the current record slide back out.
        pending_.clear();
    }

    const float slideDur = fast_ ? 0.3f : 0.8f;
    slide_ = std::clamp(slide_ + (out_ ? 1.0f : -1.0f) * dt / slideDur, 0.0f, 1.0f);

    // Disc fully retracted with an album buffered: begin the sleeve flip.
    if (slide_ <= 0.0f && !pending_.empty() && !flipping_) {
        flipping_ = true;
        flip_ = 0;
    }

    if (flipping_) {
        const float flipDur = fast_ ? 0.3f : 0.55f;
        flip_ = std::min(1.0f, flip_ + dt / flipDur);
        // Edge-on (the turn's halfway point): the new cover now faces us, so
        // swap which album the art draws.
        if (flip_ >= 0.5f && !pending_.empty()) {
            displayed_ = pending_;
            pending_.clear();
        }
        if (flip_ >= 1.0f) flipping_ = false;
    }

    // Steady state (settled, nothing buffered): disc follows playback
    if (!flipping_ && pending_.empty()) {
        out_ = enabled && playing;
        if (slide_ == (out_ ? 1.0f : 0.0f)) fast_ = false;  // sequence settled
    }

    if (playing) spinDeg_ = std::fmod(spinDeg_ + dt / kSpinSecondsPerRev * 360.0f, 360.0f);
}

bool Vinyl::Animating() const {
    return flipping_ || !pending_.empty() || slide_ != (out_ ? 1.0f : 0.0f);
}

void Vinyl::EnsureTextures() {
    if (body_.id == 0) body_ = GenBodyTexture();
    if (sheen_.id == 0) sheen_ = GenSheenTexture();
}

void Vinyl::Draw(Rectangle artRect, Color labelAccent, Color labelDominant) {
    // Hidden entirely while the sleeve flips — the record is tucked away inside
    // it, so only the turning cover shows.
    if (flipping_) return;
    const float e = EaseOut(slide_);
    // While retracting for an album change the disc stays opaque as it slides
    // under the art; plain pause-retracts fade out with the slide.
    const float alpha = !pending_.empty() ? 1.0f : e;
    if (alpha <= 0.004f) return;
    EnsureTextures();

    const float d = artRect.width * kDiscScale;
    const Vector2 c{artRect.x + artRect.width / 2 + artRect.width * kSlideFrac * e,
                    artRect.y + artRect.height / 2};
    const Rectangle src{0, 0, kTexSize, kTexSize};

    DrawTexturePro(body_, src, Rectangle{c.x - d / 2, c.y - d / 2, d, d}, Vector2{0, 0}, 0,
                   Fade(WHITE, alpha));
    DrawTexturePro(sheen_, src, Rectangle{c.x, c.y, d, d}, Vector2{d / 2, d / 2}, spinDeg_,
                   Fade(WHITE, alpha));

    // Center label: accent -> dominant radial, 20% of the disc, spindle hole
    const float labelR = d * 0.10f;
    DrawCircleGradient(c, labelR, Fade(labelAccent, alpha), Fade(labelDominant, alpha));
    DrawCircleV(c, labelR * 0.14f, Fade(Color{17, 17, 17, 255}, alpha));
}

void Vinyl::Unload() {
    if (body_.id != 0) UnloadTexture(body_);
    if (sheen_.id != 0) UnloadTexture(sheen_);
    body_ = Texture2D{};
    sheen_ = Texture2D{};
}
