#pragma once

#include "raylib.h"

// Audio-reactive frequency bars. Taps the mixed output via raylib's audio
// processor callback, runs an FFT on the main thread each frame.
class Visualizer {
public:
    static constexpr int kBars = 72;

    void Attach();   // call after InitAudioDevice()
    void Detach();   // call before CloseAudioDevice()
    void Update(float dt, bool playing);
    void DrawBars(Rectangle area, Color color) const;
    // Overall low-frequency energy 0..1, for ambient glow effects.
    float BassLevel() const { return bass_; }

private:
    float bars_[kBars] = {};
    float peaks_[kBars] = {};
    float bass_ = 0.0f;
    bool attached_ = false;
};
