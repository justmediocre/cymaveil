#pragma once

#include <string>

#include "raylib.h"

#include "contour.h"

// Audio-reactive visualizer, ported from the web app's src/lib/visualizers/*
// on top of an emulation of the Web Audio AnalyserNode it reads from
// (fftSize 512, smoothingTimeConstant 0.4, byte frequency data over
// -100..-30 dB). Taps the mixed output via raylib's audio processor callback
// and runs the FFT on the main thread each frame.
class Visualizer {
public:
    enum class Style { ContourBars, FullSurface, RadialBurst, Waveform, MirroredBars };
    static constexpr int kStyleCount = 5;
    static const char* StyleName(Style s);        // settings key ("full-surface", ...)
    static const char* StyleLabel(Style s);       // human label ("Full Surface", ...)
    static Style StyleFromName(const std::string& name, Style fallback);

    // computeFrameStyle() port: the colours and alpha multipliers every style
    // shares. `secondary` (when non-null) drives the bar cores for the
    // two-tone look; hasDepthMask adds the 6% / 4% insets.
    struct FrameStyle {
        Color glow;
        Color core;
        float glowAlphaMul;
        float coreAlphaMul;
        float padX;
        float padBot;
    };
    static FrameStyle ComputeFrameStyle(float intensity01, Color accent, const Color* secondary,
                                        bool hasDepthMask);

    void Attach();   // call after InitAudioDevice()
    void Detach();   // call before CloseAudioDevice()
    void Update(float dt, bool playing);
    // Draws the style into `area` (the album art rect); contour is only read
    // by ContourBars.
    void Draw(Style style, Rectangle area, const FrameStyle& st, const ContourData* contour);
    // VisualizerBackground.tsx: mean of the three lowest bins (~0-258 Hz), 0..1.
    float BassEnergy() const { return bass_; }

private:
    static constexpr int kFft = 512;
    static constexpr int kBins = kFft / 2;
    void DrawFullSurface(Rectangle a, const FrameStyle& st);
    void DrawMirrored(Rectangle a, const FrameStyle& st);
    void DrawRadial(Rectangle a, const FrameStyle& st);
    void DrawWaveform(Rectangle a, const FrameStyle& st);
    void DrawContour(Rectangle a, const FrameStyle& st, const ContourData* contour);
    float Sample(int index, float t);  // sampleSmoothed() port
    void ResetSmoothing(Style s);

    float mag_[kBins] = {};       // analyser's smoothed magnitudes
    float bytes_[kBins] = {};     // getByteFrequencyData / 255
    float timeDomain_[kFft] = {}; // getByteTimeDomainData / 255
    float smoothed_[256] = {};    // per-style bar smoothing buffer
    float wave_[128] = {};
    Style smoothedFor_ = Style::FullSurface;
    bool smoothedInit_ = false;
    float bass_ = 0.0f;
    bool attached_ = false;
};
