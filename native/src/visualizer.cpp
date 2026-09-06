#include "visualizer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <mutex>
#include <vector>

#include "fft.h"

namespace {

constexpr int kFftSize = 512;
constexpr size_t kRing = kFftSize * 4;
// barHelpers.ts
constexpr float kUsableFraction = 0.93f;
constexpr float kLogScaleExp = 1.5f;
constexpr float kSmoothRetain = 0.4f;
constexpr float kLineShadow = 6.0f, kLineGlow = 3.0f, kLineCore = 1.5f;

// Mono ring buffer fed from the audio thread.
float g_ring[kRing];
size_t g_writePos = 0;
std::mutex g_ringMutex;

void MixedProcessor(void* bufferData, unsigned int frames) {
    const float* samples = static_cast<const float*>(bufferData);  // stereo interleaved
    std::lock_guard<std::mutex> lock(g_ringMutex);
    for (unsigned int i = 0; i < frames; i++) {
        g_ring[g_writePos] = 0.5f * (samples[i * 2] + samples[i * 2 + 1]);
        g_writePos = (g_writePos + 1) % kRing;
    }
}

Color Rgb(float r, float g, float b, float a = 1.0f) {
    return Color{static_cast<unsigned char>(std::clamp(r, 0.0f, 255.0f)),
                 static_cast<unsigned char>(std::clamp(g, 0.0f, 255.0f)),
                 static_cast<unsigned char>(std::clamp(b, 0.0f, 255.0f)),
                 static_cast<unsigned char>(std::clamp(a, 0.0f, 1.0f) * 255.0f)};
}

// saturateAndBrighten() from colorUtils.ts
Color SaturateAndBrighten(Color c, float intensity) {
    const float maxC = std::max({static_cast<float>(c.r), static_cast<float>(c.g),
                                 static_cast<float>(c.b), 1.0f});
    const float satBoost = 0.3f + intensity * 0.4f;
    const float brighten = 0.15f + intensity * 0.15f;
    const auto chan = [&](unsigned char v) {
        const float sat = std::min(255.0f, v + (v / maxC) * 255.0f * satBoost);
        return std::round(sat + (255.0f - sat) * brighten);
    };
    return Rgb(chan(c.r), chan(c.g), chan(c.b));
}

// Round-capped stroke (canvas lineCap: 'round').
void Stroke(Vector2 a, Vector2 b, float width, Color col) {
    DrawLineEx(a, b, width, col);
    DrawCircleV(a, width / 2, col);
    DrawCircleV(b, width / 2, col);
}

// Canvas fillRect with a vertical linear gradient.
void GradRect(float x, float y, float w, float h, Color top, Color bottom) {
    if (w <= 0 || h <= 0) return;
    DrawRectangleGradientV(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)),
                           static_cast<int>(std::ceil(w)), static_cast<int>(std::ceil(h)), top, bottom);
}

}  // namespace

const char* Visualizer::StyleName(Style s) {
    switch (s) {
        case Style::ContourBars: return "contour-bars";
        case Style::FullSurface: return "full-surface";
        case Style::RadialBurst: return "radial-burst";
        case Style::Waveform: return "waveform";
        case Style::MirroredBars: return "mirrored-bars";
    }
    return "full-surface";
}

const char* Visualizer::StyleLabel(Style s) {
    switch (s) {
        case Style::ContourBars: return "Contour Bars";
        case Style::FullSurface: return "Full Surface";
        case Style::RadialBurst: return "Radial Burst";
        case Style::Waveform: return "Waveform";
        case Style::MirroredBars: return "Mirrored Bars";
    }
    return "Full Surface";
}

Visualizer::Style Visualizer::StyleFromName(const std::string& name, Style fallback) {
    for (int i = 0; i < kStyleCount; i++) {
        if (name == StyleName(static_cast<Style>(i))) return static_cast<Style>(i);
    }
    return fallback;
}

Visualizer::FrameStyle Visualizer::ComputeFrameStyle(float intensity01, Color accent,
                                                     const Color* secondary, bool hasDepthMask) {
    FrameStyle st;
    st.glow = SaturateAndBrighten(accent, intensity01);
    if (secondary != nullptr) {
        st.core = SaturateAndBrighten(*secondary, intensity01);
    } else {
        const float coreBrighten = 0.3f + intensity01 * 0.3f;
        st.core = Rgb(std::round(st.glow.r + (255 - st.glow.r) * coreBrighten),
                      std::round(st.glow.g + (255 - st.glow.g) * coreBrighten),
                      std::round(st.glow.b + (255 - st.glow.b) * coreBrighten));
    }
    st.glowAlphaMul = 0.3f + intensity01 * 0.7f;
    st.coreAlphaMul = 0.4f + intensity01 * 0.6f;
    st.padX = hasDepthMask ? 0.06f : 0.0f;
    st.padBot = hasDepthMask ? 0.04f : 0.0f;
    return st;
}

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
    (void)dt;
    if (!playing) {
        // The web analyser keeps its last smoothed frame while paused but the
        // canvas is cleared; decay ours so a resume doesn't pop.
        for (int i = 0; i < kBins; i++) {
            mag_[i] *= 0.85f;
            bytes_[i] = std::max(0.0f, bytes_[i] - 2.5f * dt);
        }
        bass_ = 0.0f;
        return;
    }

    float re[kFftSize], im[kFftSize];
    {
        std::lock_guard<std::mutex> lock(g_ringMutex);
        const size_t start = (g_writePos + kRing - kFftSize) % kRing;
        for (int i = 0; i < kFftSize; i++) {
            const float s = g_ring[(start + i) % kRing];
            re[i] = s;
            im[i] = 0.0f;
            // getByteTimeDomainData: 128 + sample*128, here normalized to 0..1
            timeDomain_[i] = std::clamp(0.5f + s * 0.5f, 0.0f, 1.0f);
        }
    }
    // RealtimeAnalyser: Blackman window, FFT, |X|/N, then exponential smoothing
    // (k = smoothingTimeConstant = 0.4) on the linear magnitudes.
    for (int i = 0; i < kFftSize; i++) {
        const float x = static_cast<float>(i) / (kFftSize - 1);
        const float w = 0.42f - 0.5f * std::cos(2 * PI * x) + 0.08f * std::cos(4 * PI * x);
        re[i] *= w;
    }
    Fft(re, im, kFftSize);
    const float scale = 1.0f / kFftSize;
    for (int i = 0; i < kBins; i++) {
        const float m = std::sqrt(re[i] * re[i] + im[i] * im[i]) * scale;
        mag_[i] = 0.4f * mag_[i] + 0.6f * m;
        // getByteFrequencyData: dB over minDecibels -100 .. maxDecibels -30
        const float db = 20.0f * std::log10(std::max(mag_[i], 1e-12f));
        bytes_[i] = std::clamp((db + 100.0f) / 70.0f, 0.0f, 1.0f);
    }
    bass_ = (bytes_[0] + bytes_[1] + bytes_[2]) / 3.0f;
}

float Visualizer::Sample(int index, float t) {
    const int binCount = static_cast<int>(std::floor(kBins * kUsableFraction));
    const int logIndex = static_cast<int>(std::floor(std::pow(t, kLogScaleExp) * (binCount - 1)));
    const float raw = bytes_[std::clamp(logIndex, 0, kBins - 1)];
    float& s = smoothed_[std::clamp(index, 0, 255)];
    s = s * kSmoothRetain + raw * (1.0f - kSmoothRetain);
    return s;
}

void Visualizer::ResetSmoothing(Style s) {
    if (smoothedInit_ && smoothedFor_ == s) return;
    std::memset(smoothed_, 0, sizeof(smoothed_));
    for (float& v : wave_) v = 0.5f;
    smoothedFor_ = s;
    smoothedInit_ = true;
}

void Visualizer::Draw(Style style, Rectangle area, const FrameStyle& st, const ContourData* contour) {
    ResetSmoothing(style);
    switch (style) {
        case Style::FullSurface: DrawFullSurface(area, st); break;
        case Style::MirroredBars: DrawMirrored(area, st); break;
        case Style::RadialBurst: DrawRadial(area, st); break;
        case Style::Waveform: DrawWaveform(area, st); break;
        case Style::ContourBars: DrawContour(area, st, contour); break;
    }
}

void Visualizer::DrawFullSurface(Rectangle a, const FrameStyle& st) {
    constexpr int kCount = 48;
    const float w = a.width, h = a.height;
    const float insetL = w * st.padX, insetB = h * st.padBot;
    const float areaW = w - 2 * insetL, areaH = h - insetB;
    const float barWidth = areaW / kCount, gap = barWidth * 0.15f, bw = barWidth - gap;

    struct Bar {
        float x, y, ey, alpha;
    };
    Bar bars[kCount];
    int n = 0;
    for (int i = 0; i < kCount; i++) {
        const float v = Sample(i, static_cast<float>(i + 1) / (kCount + 1));
        if (v < 0.02f) continue;
        const float barH = v * areaH;
        bars[n++] = {a.x + insetL + i * barWidth + gap / 2, a.y + areaH - barH, a.y + areaH,
                     0.4f + v * 0.6f};
    }
    // Shadow pass
    for (int i = 0; i < n; i++) {
        const Bar& b = bars[i];
        GradRect(b.x - 3, b.y - 3, bw + 6, b.ey - b.y + 6, Rgb(0, 0, 0, b.alpha * 0.35f), Rgb(0, 0, 0, 0));
    }
    // Glow pass
    for (int i = 0; i < n; i++) {
        const Bar& b = bars[i];
        GradRect(b.x - 2, b.y - 2, bw + 4, b.ey - b.y + 4,
                 Rgb(st.glow.r, st.glow.g, st.glow.b, b.alpha * st.glowAlphaMul),
                 Rgb(st.glow.r, st.glow.g, st.glow.b, 0));
    }
    // Core pass: core -> glow at 70% -> transparent
    for (int i = 0; i < n; i++) {
        const Bar& b = bars[i];
        const float hh = b.ey - b.y, split = hh * 0.7f;
        GradRect(b.x, b.y, bw, split, Rgb(st.core.r, st.core.g, st.core.b, b.alpha * st.coreAlphaMul),
                 Rgb(st.glow.r, st.glow.g, st.glow.b, b.alpha * st.coreAlphaMul * 0.8f));
        GradRect(b.x, b.y + split, bw, hh - split,
                 Rgb(st.glow.r, st.glow.g, st.glow.b, b.alpha * st.coreAlphaMul * 0.8f),
                 Rgb(st.glow.r, st.glow.g, st.glow.b, 0));
    }
}

void Visualizer::DrawMirrored(Rectangle a, const FrameStyle& st) {
    constexpr int kCount = 48;
    const float w = a.width, h = a.height;
    const float insetL = w * st.padX;
    const float areaW = w - 2 * insetL;
    const float centerY = a.y + h / 2;
    const float barWidth = areaW / kCount, gap = barWidth * 0.15f, bw = barWidth - gap;
    const float maxHalf = (h / 2) * 0.85f;
    float vals[kCount];
    for (int i = 0; i < kCount; i++) vals[i] = Sample(i, static_cast<float>(i + 1) / (kCount + 1));

    for (int i = 0; i < kCount; i++) {
        const float v = vals[i];
        if (v < 0.02f) continue;
        const float half = v * maxHalf, x = a.x + insetL + i * barWidth + gap / 2, alpha = 0.4f + v * 0.6f;
        DrawRectangleRec(Rectangle{x - 3, centerY - half - 3, bw + 6, half * 2 + 6}, Rgb(0, 0, 0, alpha * 0.35f));
    }
    for (int i = 0; i < kCount; i++) {
        const float v = vals[i];
        if (v < 0.02f) continue;
        const float half = v * maxHalf, x = a.x + insetL + i * barWidth + gap / 2, alpha = 0.4f + v * 0.6f;
        const Color g0 = Rgb(st.glow.r, st.glow.g, st.glow.b, 0);
        const Color g1 = Rgb(st.glow.r, st.glow.g, st.glow.b, alpha * st.glowAlphaMul);
        GradRect(x - 2, centerY - half - 2, bw + 4, half + 2, g1, g0);
        GradRect(x - 2, centerY, bw + 4, half + 2, g0, g1);
    }
    for (int i = 0; i < kCount; i++) {
        const float v = vals[i];
        if (v < 0.02f) continue;
        const float half = v * maxHalf, x = a.x + insetL + i * barWidth + gap / 2, alpha = 0.4f + v * 0.6f;
        const Color g0 = Rgb(st.glow.r, st.glow.g, st.glow.b, 0);
        const Color gm = Rgb(st.glow.r, st.glow.g, st.glow.b, alpha * st.coreAlphaMul * 0.8f);
        const Color c1 = Rgb(st.core.r, st.core.g, st.core.b, alpha * st.coreAlphaMul);
        // 0 -> 0.3 -> 1 stops: transparent at the centre, glow at 30%, core at the tip
        GradRect(x, centerY - half, bw, half * 0.7f, c1, gm);
        GradRect(x, centerY - half * 0.3f, bw, half * 0.3f, gm, g0);
        GradRect(x, centerY, bw, half * 0.3f, g0, gm);
        GradRect(x, centerY + half * 0.3f, bw, half * 0.7f, gm, c1);
    }
}

void Visualizer::DrawRadial(Rectangle a, const FrameStyle& st) {
    constexpr int kCount = 64;
    const float cx = a.x + a.width / 2, cy = a.y + a.height / 2;
    const float maxRadius = std::min(a.width, a.height) * 0.45f;
    const float innerRadius = maxRadius * 0.15f;
    float vals[kCount];
    for (int i = 0; i < kCount; i++) vals[i] = Sample(i, static_cast<float>(i + 1) / (kCount + 1));
    const float step = 2 * PI / kCount;
    const auto pass = [&](float width, const auto& colorFor) {
        for (int i = 0; i < kCount; i++) {
            const float v = vals[i];
            if (v < 0.02f) continue;
            const float ang = i * step - PI / 2;
            const float len = innerRadius + v * (maxRadius - innerRadius);
            const float c = std::cos(ang), s = std::sin(ang);
            Stroke(Vector2{cx + c * innerRadius, cy + s * innerRadius}, Vector2{cx + c * len, cy + s * len},
                   width, colorFor(0.5f + v * 0.5f));
        }
    };
    pass(kLineShadow, [&](float alpha) { return Rgb(0, 0, 0, alpha * 0.35f); });
    pass(kLineGlow, [&](float alpha) { return Rgb(st.glow.r, st.glow.g, st.glow.b, alpha * st.glowAlphaMul); });
    pass(kLineCore, [&](float alpha) { return Rgb(st.core.r, st.core.g, st.core.b, alpha * st.coreAlphaMul); });
}

void Visualizer::DrawWaveform(Rectangle a, const FrameStyle& st) {
    constexpr int kPoints = 128;
    const float step = static_cast<float>(kFftSize) / kPoints;
    for (int i = 0; i < kPoints; i++) {
        const float raw = timeDomain_[static_cast<int>(i * step)];
        wave_[i] = wave_[i] * 0.3f + raw * 0.7f;
    }
    const float marginX = a.width * 0.05f, drawW = a.width - 2 * marginX;
    const float centerY = a.y + a.height / 2, amplitude = a.height * 0.4f;
    // Quadratic curves through midpoints, flattened to short segments.
    std::vector<Vector2> path;
    path.reserve(kPoints * 4);
    auto pt = [&](int i) {
        return Vector2{a.x + marginX + static_cast<float>(i) / (kPoints - 1) * drawW,
                       centerY + (wave_[i] - 0.5f) * 2 * amplitude};
    };
    Vector2 cur = pt(0);
    path.push_back(cur);
    for (int i = 0; i < kPoints - 1; i++) {
        const Vector2 ctrl = pt(i), next = pt(i + 1);
        const Vector2 end{(ctrl.x + next.x) / 2, (ctrl.y + next.y) / 2};
        for (int k = 1; k <= 3; k++) {
            const float t = k / 3.0f, u = 1 - t;
            path.push_back(Vector2{u * u * cur.x + 2 * u * t * ctrl.x + t * t * end.x,
                                   u * u * cur.y + 2 * u * t * ctrl.y + t * t * end.y});
        }
        cur = end;
    }
    path.push_back(pt(kPoints - 1));
    const auto strokePath = [&](float width, Color col) {
        for (size_t i = 0; i + 1 < path.size(); i++) DrawLineEx(path[i], path[i + 1], width, col);
        for (const auto& p : path) DrawCircleV(p, width / 2, col);
    };
    strokePath(kLineShadow, Rgb(0, 0, 0, 0.35f * st.glowAlphaMul));
    strokePath(kLineGlow, Rgb(st.glow.r, st.glow.g, st.glow.b, st.glowAlphaMul));
    strokePath(kLineCore, Rgb(st.core.r, st.core.g, st.core.b, st.coreAlphaMul));
}

void Visualizer::DrawContour(Rectangle a, const FrameStyle& st, const ContourData* contour) {
    if (contour == nullptr || contour->points.empty()) return;
    const float w = a.width, h = a.height;
    const float span = contour->span;
    const float maxBarLen = 20 + span * 40;
    const float shadowW = 7 + span * 4, glowW = 4 + span * 3, coreW = 1.5f + span;
    const int n = static_cast<int>(contour->points.size());
    struct Bar {
        Vector2 p, e;
        float alpha;
    };
    std::vector<Bar> bars;
    bars.reserve(n);
    const float minX = w * st.padX, maxX = w - w * st.padX, maxY = h - h * st.padBot;
    for (int i = 0; i < n; i++) {
        const float v = Sample(i, static_cast<float>(i + 1) / (n + 1));
        if (v < 0.02f) continue;
        const ContourPoint& p = contour->points[i];
        const float px = p.x * w, py = p.y * h;
        float len = v * maxBarLen;
        if (p.nx != 0) {
            const float maxLenX = p.nx > 0 ? (maxX - px) / p.nx : (minX - px) / p.nx;
            if (maxLenX > 0 && maxLenX < len) len = maxLenX;
        }
        if (p.ny != 0) {
            const float maxLenY = p.ny > 0 ? (maxY - py) / p.ny : -py / p.ny;
            if (maxLenY > 0 && maxLenY < len) len = maxLenY;
        }
        bars.push_back({Vector2{a.x + px, a.y + py}, Vector2{a.x + px + p.nx * len, a.y + py + p.ny * len},
                        0.5f + v * 0.5f});
    }
    for (const Bar& b : bars) Stroke(b.p, b.e, shadowW, Rgb(0, 0, 0, b.alpha * 0.35f));
    for (const Bar& b : bars)
        Stroke(b.p, b.e, glowW, Rgb(st.glow.r, st.glow.g, st.glow.b, b.alpha * st.glowAlphaMul));
    for (const Bar& b : bars)
        Stroke(b.p, b.e, coreW, Rgb(st.core.r, st.core.g, st.core.b, b.alpha * st.coreAlphaMul));
}
