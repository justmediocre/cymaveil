#include "artview.h"

#include <algorithm>
#include <cmath>

#include "rlgl.h"

#include "ui.h"

namespace {

constexpr float kVinylTravel = 65.0f / 340.0f;  // x: 65px at the web's 340px art
constexpr float kVinylScale = 0.96f;            // web uses 108%, but that pokes past the
                                                // art; stay inside it even at the bass-zoom
                                                // minimum (0.97) so only the slide-out shows
constexpr float kSpinSecondsPerRev = 1.8f;
constexpr float kRadius = 16.0f;                // rounded-2xl
constexpr float kBassHitThreshold = 0.6f;
constexpr float kBassHitDebounce = 0.06f;

}  // namespace

bool ArtView::Animating() const {
    return phase_ != Phase::Steady || !slide_.Done() || !discAlpha_.Done() ||
           std::fabs(zoom_ - 1.0f) > 0.0005f || std::fabs(zoomVel_) > 0.0005f;
}

void ArtView::BeginSwap() {
    TraceLog(LOG_DEBUG, "ARTVIEW: swap -> %s (from %s)", pending_.path.c_str(), displayed_.path.c_str());
    // AnimatePresence mode="wait": the old cover fully exits before the new
    // one enters. Nothing to exit on the very first show.
    if (displayed_.Valid() || !firstShow_) {
        phase_ = Phase::Exiting;
    } else {
        displayed_ = pending_;
        displayedAlbumId_ = pendingAlbumId_;
        hasPending_ = false;
        phase_ = Phase::Entering;
    }
    firstShow_ = false;
    phaseT_ = 0;
    phaseDur_ = fast_ ? 0.25f : 0.6f;
    entered_ = false;
}

void ArtView::Update(float dt, const Input& in) {
    dt = std::min(dt, 1.0f / 20.0f);
    if (in.skipIntent) fast_ = true;

    const bool hasTarget = in.target != nullptr && in.target->Valid();
    const std::string targetPath = hasTarget ? in.target->path : std::string{};
    const bool targetDiffers = targetPath != displayed_.path || in.targetAlbumId != displayedAlbumId_;
    const bool pendingDiffers = !hasPending_ || pending_.path != targetPath ||
                                pendingAlbumId_ != in.targetAlbumId;

    if (targetDiffers && pendingDiffers) {
        const bool artOnly = in.targetAlbumId == displayedAlbumId_ && displayed_.Valid() && hasTarget;
        if (artOnly && phase_ == Phase::Steady) {
            // Per-track cover within one album: swap in place, no sequence.
            displayed_ = *in.target;
        } else if (hasTarget) {
            pending_ = *in.target;
            pendingAlbumId_ = in.targetAlbumId;
            hasPending_ = true;
            if (vinylOut_ && phase_ == Phase::Steady) {
                // Vinyl is showing — retract first, then swap on completion.
                phase_ = Phase::RetractingVinyl;
                vinylOut_ = false;
                slide_.Start(0.0f, fast_ ? 0.3f : 0.8f);
            } else if (phase_ == Phase::Steady) {
                BeginSwap();
            }
            // Mid-sequence: the pending art is picked up when the current
            // exit/enter finishes (the web lets the transition play out).
        }
    } else if (!targetDiffers && hasPending_ && phase_ == Phase::Steady) {
        hasPending_ = false;  // target backed out before the swap began
    }

    // Prefire: retract early before the track ends.
    if (in.prefire && vinylOut_ && phase_ == Phase::Steady && !hasPending_) {
        phase_ = Phase::RetractingVinyl;
        vinylOut_ = false;
        slide_.Start(0.0f, fast_ ? 0.3f : 0.8f);
    }

    // Vinyl retract complete
    if (phase_ == Phase::RetractingVinyl && slide_.Done()) {
        if (hasPending_) BeginSwap();
        else phase_ = Phase::Steady;
    }

    if (phase_ == Phase::Exiting || phase_ == Phase::Entering) {
        phaseT_ = std::min(1.0f, phaseT_ + dt / phaseDur_);
        if (phaseT_ >= 1.0f) {
            if (phase_ == Phase::Exiting) {
                displayed_ = pending_;
                displayedAlbumId_ = pendingAlbumId_;
                hasPending_ = false;
                phase_ = Phase::Entering;
                phaseT_ = 0;
            } else {
                // handleArtEnterComplete
                phase_ = Phase::Steady;
                entered_ = true;
                fast_ = false;
                if (hasPending_) {
                    // Another change queued up meanwhile: run it now.
                    if (vinylOut_) {
                        phase_ = Phase::RetractingVinyl;
                        vinylOut_ = false;
                        slide_.Start(0.0f, 0.8f);
                    } else {
                        BeginSwap();
                    }
                }
            }
        }
    }

    // Steady state: the disc follows playback (only once the art has entered).
    if (phase_ == Phase::Steady && entered_ && !hasPending_) {
        const bool want = in.vinylEnabled && in.playing;
        if (want != vinylOut_) {
            vinylOut_ = want;
            slide_.Start(want ? 1.0f : 0.0f, fast_ ? 0.3f : 0.8f);
        }
        if (slide_.Done()) fast_ = false;
    }
    if (!in.vinylEnabled && vinylOut_) {
        vinylOut_ = false;
        slide_.Set(0.0f);
    }
    // Opacity: 1 while out or mid-sequence, else fades with the slide.
    const bool discVisible = vinylOut_ || phase_ != Phase::Steady || hasPending_;
    if (discAlpha_.to != (discVisible ? 1.0f : 0.0f)) {
        discAlpha_.Start(discVisible ? 1.0f : 0.0f, fast_ ? 0.3f : 0.8f);
    }
    slide_.Step(dt, ui::EaseOutExpo);
    discAlpha_.Step(dt, ui::EaseOutExpo);

    if (in.playing) spinDeg_ = std::fmod(spinDeg_ + dt / kSpinSecondsPerRev * 360.0f, 360.0f);

    // Bass-hit zoom — spring physics tuned for a ~30 fps tick.
    if (!in.playing || !in.bassShake) {
        zoom_ = 1.0f;
        zoomVel_ = 0.0f;
        tickAccum_ = 0;
    } else {
        tickAccum_ += dt;
        while (tickAccum_ >= 1.0f / 30.0f) {
            tickAccum_ -= 1.0f / 30.0f;
            const double now = GetTime();
            if (in.bassEnergy > kBassHitThreshold && now - lastBassHit_ > kBassHitDebounce) {
                lastBassHit_ = now;
                const float t = (in.bassEnergy - kBassHitThreshold) / (1 - kBassHitThreshold);
                const float impulse = t * t * 0.01f;
                if (zoom_ > 1.003f || zoomVel_ > 0.002f) zoomVel_ = -impulse;
                else zoomVel_ = impulse;
            }
            const float displacement = zoom_ - 1;
            zoomVel_ += -0.3f * displacement;
            zoomVel_ *= 0.6f;
            zoom_ += zoomVel_;
            zoom_ = std::clamp(zoom_, 0.97f, 1.04f);
            if (std::fabs(zoom_ - 1) < 0.0005f && std::fabs(zoomVel_) < 0.0005f) {
                zoom_ = 1;
                zoomVel_ = 0;
            }
        }
    }
}

void ArtView::EnsureTargets(int px) {
    if (px == targetPx_ && composite_.id != 0) return;
    if (composite_.id != 0) UnloadRenderTexture(composite_);
    if (blurA_.id != 0) UnloadRenderTexture(blurA_);
    if (blurB_.id != 0) UnloadRenderTexture(blurB_);
    targetPx_ = px;
    composite_ = LoadRenderTexture(px, px);
    SetTextureFilter(composite_.texture, TEXTURE_FILTER_BILINEAR);
    blurA_ = LoadRenderTexture(px, px);
    blurB_ = LoadRenderTexture(px, px);
    SetTextureFilter(blurA_.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(blurB_.texture, TEXTURE_FILTER_BILINEAR);
    if (reflection_.id == 0) {
        // linear-gradient(to right, transparent, X, transparent) baked as alpha
        Image img = GenImageColor(256, 4, BLANK);
        auto* p = static_cast<Color*>(img.data);
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 256; x++) {
                const float t = x / 255.0f;
                p[y * 256 + x] = Color{255, 255, 255, static_cast<unsigned char>(255 * (1.0f - std::fabs(2 * t - 1)))};
            }
        }
        reflection_ = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(reflection_, TEXTURE_FILTER_BILINEAR);
    }
}

void ArtView::Draw(const DrawArgs& a) {
    const Rectangle r = a.artRect;
    const float size = r.width;
    const Vector2 centre{r.x + size / 2, r.y + size / 2};
    const bool coverReady = a.cover != nullptr && a.cover->id != 0;
    const float dpi = GetScreenWidth() > 0
                          ? static_cast<float>(GetRenderWidth()) / static_cast<float>(GetScreenWidth())
                          : 1.0f;
    const int px = std::clamp(static_cast<int>(std::lround(size * dpi)), 64, 1024);
    EnsureTargets(px);

    // ── Reflection beneath: 60% of the view wide, 48px tall, its bottom 32px
    // below the art, blur 40px; opacity 0.5 playing / 0.2 paused over 1s ──
    if (displayed_.Valid() && reflection_.id != 0) {
        const float reflA = ui::Ease("np.art.refl", a.playing ? 0.5f : 0.2f, 1.0f);
        const float rw = std::max(size, a.viewWidth) * 0.6f;
        const Rectangle refl{centre.x - rw / 2, r.y + size + 32 - 48, rw, 48};
        const float mix = a.lightTheme ? 0.31f : 0.19f;
        ui::RoundedTexture(reflection_, Rectangle{0, 0, 256, 4}, refl, 24,
                           Fade(displayed_.dominant, mix * reflA), 40.0f);
    }

    // ── Vinyl disc ──
    const float slide = slide_.value;
    const float discA = discAlpha_.value;
    if (discA > 0.004f && displayed_.Valid()) {
        const Vector2 c{centre.x + size * kVinylTravel * slide, centre.y};
        vinyl_.Draw(c, size * kVinylScale, spinDeg_, discA, displayed_.accent, displayed_.dominant);
    }

    // ── Cover transition parameters ──
    float opacity = 1.0f, scale = 1.0f, blurPx = 0.0f;
    if (phase_ == Phase::Exiting) {
        const float e = ui::EaseOutExpo(phaseT_);
        opacity = 1.0f - e;
        scale = 1.0f - 0.05f * e;
        blurPx = 8.0f * e;
    } else if (phase_ == Phase::Entering) {
        const float e = ui::EaseOutExpo(phaseT_);
        opacity = e;
        scale = 0.92f + 0.08f * e;
        blurPx = 10.0f * (1.0f - e);
    } else if (!entered_ && displayed_.Valid()) {
        opacity = 0.0f;  // first frame before the entrance starts
    }
    if (!displayed_.Valid()) return;

    // Bass zoom + its motion blur (blur(|zoom-1| * 250px))
    const float zoomD = std::fabs(zoom_ - 1.0f);
    scale *= zoom_;
    if (zoomD > 0.0005f) blurPx = std::max(blurPx, zoomD * 250.0f);

    const Rectangle dst = ui::Scaled(r, scale);
    const float radius = kRadius * scale;

    // ── Shadows: resting drop shadow + accent underglow ──
    ui::Shadow(dst, radius, 0, 25, 50, -12, Fade(BLACK, 0.25f * opacity));
    const float glowA = ui::Ease("np.art.glow", a.ambientGlow && a.playing ? 1.0f : 0.0f, 0.4f);
    if (glowA > 0.01f) {
        const float blur = a.lightTheme ? 12.0f : 18.0f, spread = a.lightTheme ? 3.0f : 4.0f;
        const float alpha = a.lightTheme ? 0.20f : 0.35f;
        ui::Shadow(dst, radius, 0, 0, blur, spread, Fade(displayed_.accent, alpha * glowA * opacity));
    }

    // ── Composite: cover + overlay (visualizer, depth foreground) ──
    const float s = px / size;
    BeginTextureMode(composite_);
    ClearBackground(ui::theme.elevated);
    // Translucent overlay draws (bar gradients, feathered mask) must not eat
    // into the target's alpha: blend colour normally but keep alpha at
    // dst + src·(1-dst), so the composite stays opaque.
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA,
                              RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    rlPushMatrix();
    rlScalef(s, s, 1.0f);
    const Rectangle local{0, 0, size, size};
    if (coverReady) {
        DrawTexturePro(*a.cover, ui::CoverSrc(*a.cover), local, Vector2{0, 0}, 0, WHITE);
        if (ArtEntered() && a.drawOverlay) a.drawOverlay(local);
    } else {
        ui::IconDisc(Vector2{size / 2, size / 2}, size * 0.2f, ui::theme.textTertiary, 1.5f);
    }
    rlPopMatrix();
    EndBlendMode();
    EndTextureMode();

    const Texture2D* shown = &composite_.texture;
    Rectangle src{0, 0, static_cast<float>(px), -static_cast<float>(px)};
    if (blurPx > 0.5f) {
        // CSS blur(σ) is a gaussian of sigma σ px. The 9-tap kernel at spread
        // s has sigma ≈ 1.6·s texels; two passes add in quadrature.
        const int passes = blurPx > 6.0f ? 2 : 1;
        const float spread = std::clamp(blurPx * s / (1.6f * std::sqrt(static_cast<float>(passes))), 0.4f, 4.5f);
        blur_.Run(composite_.texture, src, blurA_, blurB_, spread, passes);
        shown = &blurB_.texture;
        src = Rectangle{0, 0, static_cast<float>(blurB_.texture.width), -static_cast<float>(blurB_.texture.height)};
    }
    ui::RoundedTexture(*shown, src, dst, radius, Fade(WHITE, opacity));
    // ── Hover inner glow (inset 0 0 60px accent @ 8% / 19%) ──
    const float hov = ui::Ease("np.art.hover", a.hovered ? 1.0f : 0.0f, 0.3f);
    if (hov > 0.01f) {
        ui::InnerShadow(dst, radius, 60, Fade(displayed_.accent, (a.lightTheme ? 0.19f : 0.08f) * hov * opacity));
    }
}

void ArtView::Unload() {
    vinyl_.Unload();
    blur_.Unload();
    if (composite_.id != 0) UnloadRenderTexture(composite_);
    if (blurA_.id != 0) UnloadRenderTexture(blurA_);
    if (blurB_.id != 0) UnloadRenderTexture(blurB_);
    if (reflection_.id != 0) UnloadTexture(reflection_);
    composite_ = RenderTexture2D{};
    blurA_ = RenderTexture2D{};
    blurB_ = RenderTexture2D{};
    reflection_ = Texture2D{};
    targetPx_ = 0;
}
