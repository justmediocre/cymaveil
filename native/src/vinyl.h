#pragma once

#include <string>

#include "raylib.h"

// The vinyl disc behind the Now Playing album art, ported from AlbumArt.tsx:
// slides out from behind the art while playing (65px at the web's 340px art,
// scaled proportionally), retracts on pause, spins at 1.8s/rev with a
// rotating light-catch sheen. Album changes are sequenced like a record swap:
// retract the disc into its sleeve -> flip the sleeve over (the new cover rides
// its back face) -> extend the new disc again. Manual skips use the faster
// durations ('skip' transition intent in the web app).
class Vinyl {
public:
    // targetAlbumId: the album the art should settle on ("" when nothing
    // loaded). Callers may pre-fire a transition by passing the upcoming
    // album's id a beat early so the swap lines up with the audio change.
    // skipIntent: true on frames where the user manually changed tracks.
    // enabled: the vinylDisc setting; the flip sequence runs even when the disc
    // itself is hidden, matching the web component.
    void Update(float dt, const std::string& targetAlbumId, bool playing, bool skipIntent,
                bool enabled);

    // The album currently facing the viewer. It swaps to the buffered album at
    // the flip's midpoint, so it lags the target through retract -> flip.
    const std::string& DisplayedAlbumId() const { return displayed_; }
    // Flip progress 0..1 (1 == settled). The art's horizontal scale is
    // |cos(progress * PI)|: full -> edge-on at 0.5 -> full again.
    float FlipProgress() const { return flip_; }
    // True until the flip fully settles; layers on the art (visualizer, depth
    // foreground) hold off until then.
    bool ArtEntered() const { return !flipping_; }

    bool Animating() const;
    void Draw(Rectangle artRect, Color labelAccent, Color labelDominant);
    void Unload();

private:
    void EnsureTextures();

    std::string displayed_;
    std::string pending_;
    bool out_ = false;       // slide target
    float slide_ = 0;        // raw progress 0..1 toward "out"
    bool flipping_ = false;  // the sleeve is mid-turn
    float flip_ = 1;         // flip progress 0..1 (1 == settled, art full width)
    float spinDeg_ = 0;
    bool fast_ = false;      // skip-intent durations for the current sequence

    Texture2D body_{};
    Texture2D sheen_{};
};
