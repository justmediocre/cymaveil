#pragma once

#include <functional>
#include <string>

#include "raylib.h"

#include "blur.h"
#include "library.h"
#include "vinyl.h"

// The Now Playing album art stack, ported from AlbumArt.tsx: the cover with
// its rounded corners, resting shadow, accent underglow and reflection; the
// vinyl disc that slides out while playing; the sequenced art swap (retract
// the record, fade/blur/scale the old cover out and the new one in, extend
// the record again); the bass-hit zoom; and the hover inner glow. The art,
// visualizer and depth-mask foreground are composited into one offscreen
// target so the corner clipping, blur and bass zoom apply to them together —
// the frame itself never resizes on a bass hit, matching the web, where the
// scaled wrapper lives inside a fixed-size `overflow-hidden` parent.
class ArtView {
public:
    struct Input {
        const Art* target = nullptr;   // art the current track wants shown
        std::string targetAlbumId;     // to tell an album change from a per-track cover swap
        bool playing = false;
        bool skipIntent = false;       // user-initiated track change this frame
        bool prefire = false;          // track is about to end on a different album
        bool vinylEnabled = true;
        bool bassShake = true;
        float bassEnergy = 0.0f;       // 0..1
    };

    void Update(float dt, const Input& in);

    // The art actually on screen (lags the target through the swap sequence).
    const Art& Displayed() const { return displayed_; }
    bool HasDisplayed() const { return displayed_.Valid(); }
    // True once the entrance animation has finished — the depth foreground
    // and visualizer only draw then, like the web component.
    bool ArtEntered() const { return phase_ == Phase::Steady && entered_; }
    bool Animating() const;

    // Draws the stack around artRect. `cover` is the displayed art's texture
    // (nullptr while decoding); `drawOverlay` paints the visualizer + depth
    // foreground into the composite in art-local coordinates (0,0,size,size)
    // and is only invoked once the cover is resident and entered.
    struct DrawArgs {
        Rectangle artRect;
        float viewWidth = 0;           // the Now Playing column width (reflection is 60% of it)
        const Texture2D* cover = nullptr;
        bool playing = false;
        bool ambientGlow = true;
        bool lightTheme = false;
        bool hovered = false;
        std::function<void(Rectangle local)> drawOverlay;
    };
    void Draw(const DrawArgs& a);
    void Unload();

private:
    enum class Phase { Steady, RetractingVinyl, Exiting, Entering };

    struct Tween {
        float from = 0, to = 0, value = 0, t = 1, dur = 0.001f;
        void Start(float target, float seconds) {
            from = value;
            to = target;
            t = 0;
            dur = std::max(seconds, 0.001f);
        }
        void Set(float v) { from = to = value = v; t = 1; }
        bool Done() const { return t >= 1.0f; }
        void Step(float dt, float (*ease)(float)) {
            if (t >= 1.0f) return;
            t = std::min(1.0f, t + dt / dur);
            value = from + (to - from) * ease(t);
        }
    };

    void BeginSwap();
    void EnsureTargets(int px);

    Art displayed_;
    std::string displayedAlbumId_;
    Art pending_;
    std::string pendingAlbumId_;
    bool hasPending_ = false;
    Phase phase_ = Phase::Steady;
    bool entered_ = false;      // artEntered
    bool firstShow_ = true;
    bool fast_ = false;         // 'skip' durations for the sequence in flight
    bool vinylOut_ = false;
    Tween slide_;               // 0..1 disc travel
    Tween discAlpha_;
    float spinDeg_ = 0;
    float phaseT_ = 0;          // exit/enter progress 0..1
    float phaseDur_ = 0.6f;

    // Bass-hit zoom spring (AlbumArt.tsx onTick at ~30fps). `zoomShown_`
    // trails `zoom_` to stand in for the wrapper's 100ms CSS transition.
    float zoom_ = 1.0f, zoomVel_ = 0.0f, zoomShown_ = 1.0f;
    double lastBassHit_ = 0;
    float tickAccum_ = 0;

    Vinyl vinyl_;
    GaussianBlur blur_;
    RenderTexture2D composite_{};
    RenderTexture2D blurA_{};
    RenderTexture2D blurB_{};
    Texture2D reflection_{};
    int targetPx_ = 0;
};
