#pragma once

#include <string>

#include "raylib.h"

// The vinyl disc behind the Now Playing album art, ported from AlbumArt.tsx:
// slides out from behind the art while playing (65px at the web's 340px art,
// scaled proportionally), retracts on pause, spins at 1.8s/rev with a
// rotating light-catch sheen. Album changes are sequenced: retract vinyl ->
// swap art (fade/scale entrance) -> extend vinyl again. Manual skips use the
// faster durations ('skip' transition intent in the web app).
class Vinyl {
public:
    // targetAlbumId: the playing track's album ("" when nothing loaded).
    // skipIntent: true on frames where the user manually changed tracks.
    // enabled: the vinylDisc setting; the art entrance sequence runs even
    // when the disc itself is hidden, matching the web component.
    void Update(float dt, const std::string& targetAlbumId, bool playing, bool skipIntent,
                bool enabled);

    // Art is swapped only after the retract completes; render this album.
    const std::string& DisplayedAlbumId() const { return displayed_; }
    // Entrance animation state for the art (and everything layered on it).
    float ArtAlpha() const;
    float ArtScale() const;
    bool ArtEntered() const { return artEnter_ >= 1.0f; }

    bool Animating() const;
    void Draw(Rectangle artRect, Color labelAccent, Color labelDominant);
    void Unload();

private:
    void EnsureTextures();

    std::string displayed_;
    std::string pending_;
    bool out_ = false;      // slide target
    float slide_ = 0;       // raw progress 0..1 toward "out"
    float artEnter_ = 1;    // art entrance progress 0..1
    float spinDeg_ = 0;
    bool fast_ = false;     // skip-intent durations for the current sequence

    Texture2D body_{};
    Texture2D sheen_{};
};
