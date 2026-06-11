#include "visualizer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <mutex>

#include "fft.h"

namespace {

constexpr int kFftSize = 2048;
// Visual mapping only; miniaudio device rate is typically 44.1/48 kHz.
constexpr float kSampleRate = 48000.0f;
constexpr float kFreqLo = 40.0f;
constexpr float kFreqHi = 15000.0f;

// Mono ring buffer fed from the audio thread.
float g_ring[kFftSize * 4];
size_t g_writePos = 0;
std::mutex g_ringMutex;

void MixedProcessor(void* bufferData, unsigned int frames) {
    const float* samples = static_cast<const float*>(bufferData);  // stereo interleaved
    std::lock_guard<std::mutex> lock(g_ringMutex);
    for (unsigned int i = 0; i < frames; i++) {
        g_ring[g_writePos] = 0.5f * (samples[i * 2] + samples[i * 2 + 1]);
        g_writePos = (g_writePos + 1) % std::size(g_ring);
    }
}

}  // namespace

void Visualizer::Attach() {
    if (!attached_ && IsAudioDeviceReady()) {
        AttachAudioMixedProcessor(MixedProcessor);
        attached_ = true;
    }
}

void Visualizer::Detach() {
    if (attached_) {
        DetachAudioMixedProcessor(MixedProcessor);
        attached_ = false;
    }
}

void Visualizer::Update(float dt, bool playing) {
    if (!playing) {
        // Let bars decay to zero, then idle.
        for (int i = 0; i < kBars; i++) {
            bars_[i] = std::max(0.0f, bars_[i] - 2.5f * dt);
            peaks_[i] = std::max(0.0f, peaks_[i] - 0.6f * dt);
        }
        bass_ = std::max(0.0f, bass_ - 2.5f * dt);
        return;
    }

    float samples[kFftSize];
    {
        std::lock_guard<std::mutex> lock(g_ringMutex);
        const size_t n = std::size(g_ring);
        size_t start = (g_writePos + n - kFftSize) % n;
        for (int i = 0; i < kFftSize; i++) {
            samples[i] = g_ring[(start + i) % n];
        }
    }

    float mags[kFftSize / 2];
    FftMagnitudes(samples, kFftSize, mags);

    const float binHz = kSampleRate / kFftSize;
    float bassSum = 0.0f;
    for (int b = 0; b < kBars; b++) {
        const float f0 = kFreqLo * std::pow(kFreqHi / kFreqLo, static_cast<float>(b) / kBars);
        const float f1 = kFreqLo * std::pow(kFreqHi / kFreqLo, static_cast<float>(b + 1) / kBars);
        int i0 = std::max(1, static_cast<int>(f0 / binHz));
        int i1 = std::max(i0 + 1, static_cast<int>(f1 / binHz) + 1);
        i1 = std::min(i1, kFftSize / 2);
        float m = 0.0f;
        for (int i = i0; i < i1; i++) m = std::max(m, mags[i]);
        // Map to dB scale: -60 dB .. 0 dB -> 0 .. 1
        const float db = 20.0f * std::log10(m + 1e-7f);
        const float v = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
        // Fast attack, smooth release
        bars_[b] = v > bars_[b] ? bars_[b] + (v - bars_[b]) * std::min(1.0f, 30.0f * dt)
                                : std::max(v, bars_[b] - 1.8f * dt);
        peaks_[b] = std::max(bars_[b], peaks_[b] - 0.5f * dt);
        if (b < kBars / 6) bassSum += v;
    }
    const float bassNow = bassSum / (kBars / 6);
    bass_ = bassNow > bass_ ? bassNow : std::max(bassNow, bass_ - 2.0f * dt);
}

namespace {

// saturateAndBrighten() from the web app's colorUtils.ts
Color SaturateAndBrighten(Color c, float intensity) {
    const float maxC = std::max({c.r, c.g, c.b, static_cast<unsigned char>(1)});
    const float satBoost = 0.3f + intensity * 0.4f;
    const float brighten = 0.15f + intensity * 0.15f;
    const auto chan = [&](unsigned char v) {
        const float sat = std::min(255.0f, v + (v / maxC) * 255.0f * satBoost);
        return static_cast<unsigned char>(std::lround(sat + (255.0f - sat) * brighten));
    };
    return Color{chan(c.r), chan(c.g), chan(c.b), 255};
}

}  // namespace

void Visualizer::DrawFullSurface(Rectangle area, Color accent, float intensity) const {
    constexpr int kCount = 48;

    // computeFrameStyle port ('auto' color mode, single accent)
    const Color glow = SaturateAndBrighten(accent, intensity);
    const float coreBrighten = 0.3f + intensity * 0.3f;
    const Color core{static_cast<unsigned char>(glow.r + (255 - glow.r) * coreBrighten),
                     static_cast<unsigned char>(glow.g + (255 - glow.g) * coreBrighten),
                     static_cast<unsigned char>(glow.b + (255 - glow.b) * coreBrighten), 255};
    const float glowMul = 0.3f + intensity * 0.7f;
    const float coreMul = 0.4f + intensity * 0.6f;

    const float slot = area.width / kCount;
    const float gap = slot * 0.15f;
    const float barW = slot - gap;

    for (int i = 0; i < kCount; i++) {
        // Resample our kBars log-spaced bins down to 48
        const float pos = static_cast<float>(i) * (kBars - 1) / (kCount - 1);
        const int i0 = static_cast<int>(pos);
        const float frac = pos - i0;
        const float v = bars_[i0] * (1 - frac) + bars_[std::min(i0 + 1, kBars - 1)] * frac;
        if (v < 0.02f) continue;

        const float barH = v * area.height;
        const float x = area.x + i * slot + gap / 2;
        const float y = area.y + area.height - barH;
        const float alpha = 0.4f + v * 0.6f;

        // Shadow pass: darken behind the bar for separation from the art
        DrawRectangleGradientV(static_cast<int>(x - 3), static_cast<int>(y - 3),
                               static_cast<int>(barW + 6), static_cast<int>(barH + 6),
                               Fade(BLACK, alpha * 0.35f), Fade(BLACK, 0.0f));
        // Glow pass
        DrawRectangleGradientV(static_cast<int>(x - 2), static_cast<int>(y - 2),
                               static_cast<int>(barW + 4), static_cast<int>(barH + 4),
                               Fade(glow, alpha * glowMul), Fade(glow, 0.0f));
        // Core pass (3-stop gradient: core -> glow at 70% -> transparent)
        const float split = barH * 0.7f;
        DrawRectangleGradientV(static_cast<int>(x), static_cast<int>(y), static_cast<int>(barW),
                               static_cast<int>(split), Fade(core, alpha * coreMul),
                               Fade(glow, alpha * coreMul * 0.8f));
        DrawRectangleGradientV(static_cast<int>(x), static_cast<int>(y + split),
                               static_cast<int>(barW), static_cast<int>(barH - split),
                               Fade(glow, alpha * coreMul * 0.8f), Fade(glow, 0.0f));
    }
}

void Visualizer::DrawBars(Rectangle area, Color color) const {
    const float slot = area.width / kBars;
    const float gap = std::max(1.0f, slot * 0.25f);
    const float w = slot - gap;
    for (int i = 0; i < kBars; i++) {
        const float h = std::max(2.0f, bars_[i] * area.height);
        const float x = area.x + i * slot + gap * 0.5f;
        const float y = area.y + area.height - h;
        DrawRectangleGradientV(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w),
                               static_cast<int>(h), Fade(color, 0.95f), Fade(color, 0.25f));
        // Peak cap
        const float py = area.y + area.height - std::max(2.0f, peaks_[i] * area.height);
        if (py < y - 3) {
            DrawRectangleRec(Rectangle{x, py, w, 2}, Fade(color, 0.5f));
        }
    }
}
