#include "colorextract.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kSampleSize = 64;
constexpr float kSecondaryHueThreshold = 40.0f;

struct Bucket {
    int r, g, b;  // quantized (can reach 256, as in the JS rounding)
    int count = 0;
    int saturation = 0;  // from the first raw sample, matching the JS
};

float RgbToHue(float r, float g, float b) {
    const float max = std::max({r, g, b}), min = std::min({r, g, b});
    const float delta = max - min;
    if (delta == 0) return 0;
    float h;
    if (max == r) h = std::fmod(std::fmod((g - b) / delta, 6.0f) + 6.0f, 6.0f);
    else if (max == g) h = (b - r) / delta + 2.0f;
    else h = (r - g) / delta + 4.0f;
    return h * 60.0f;
}

float HueDist(float h1, float h2) {
    const float d = std::fabs(h1 - h2);
    return std::min(d, 360.0f - d);
}

unsigned char ClampByte(float v) {
    return static_cast<unsigned char>(std::clamp(std::lround(v), 0L, 255L));
}

// Boost washed-out pastel accents into a vivid version of the same hue.
Color VibrantAccent(int r, int g, int b) {
    const float rn = r / 255.0f, gn = g / 255.0f, bn = b / 255.0f;
    const float cmax = std::max({rn, gn, bn}), cmin = std::min({rn, gn, bn});
    const float delta = cmax - cmin;

    // RGB -> HSL
    float h = 0;
    if (delta > 0) {
        if (cmax == rn) h = std::fmod(std::fmod((gn - bn) / delta, 6.0f) + 6.0f, 6.0f);
        else if (cmax == gn) h = (bn - rn) / delta + 2.0f;
        else h = (rn - gn) / delta + 4.0f;
        h *= 60.0f;
    }
    float l = (cmax + cmin) / 2.0f;
    float s = delta == 0 ? 0 : delta / (1.0f - std::fabs(2.0f * l - 1.0f));

    // Only boost if there's actual hue to amplify (skip achromatic grays)
    if (s > 0.05f) {
        s = std::max(s, 0.5f);
        l = std::min(l, 0.65f);
    }

    // HSL -> RGB
    const float c = (1.0f - std::fabs(2.0f * l - 1.0f)) * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = l - c / 2.0f;
    float ro, go, bo;
    if (h < 60) { ro = c; go = x; bo = 0; }
    else if (h < 120) { ro = x; go = c; bo = 0; }
    else if (h < 180) { ro = 0; go = c; bo = x; }
    else if (h < 240) { ro = 0; go = x; bo = c; }
    else if (h < 300) { ro = x; go = 0; bo = c; }
    else { ro = c; go = 0; bo = x; }

    return Color{ClampByte((ro + m) * 255.0f), ClampByte((go + m) * 255.0f),
                 ClampByte((bo + m) * 255.0f), 255};
}

float AccentScore(const Bucket& c) {
    const float lum = (c.r + c.g + c.b) / 3.0f;
    const float lightPenalty = std::max(0.0f, (lum - 180.0f) / 75.0f);
    const float darkPenalty = std::max(0.0f, (40.0f - lum) / 40.0f);
    return c.saturation * (1.0f - std::max(lightPenalty, darkPenalty) * 0.9f);
}

}  // namespace

bool ExtractAlbumColors(const unsigned char* rgba, AlbumColors* out) {
    std::unordered_map<int, Bucket> buckets;

    const int total = kSampleSize * kSampleSize;
    for (int i = 0; i < total; i += 4) {  // sample every 4th pixel
        const int r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];

        const float brightness = (r + g + b) / 3.0f;
        if (brightness < 20 || brightness > 235) continue;

        const int qr = static_cast<int>(std::lround(r / 32.0f)) * 32;
        const int qg = static_cast<int>(std::lround(g / 32.0f)) * 32;
        const int qb = static_cast<int>(std::lround(b / 32.0f)) * 32;
        const int key = (qr << 20) | (qg << 10) | qb;

        auto [it, inserted] = buckets.try_emplace(key);
        if (inserted) {
            it->second = Bucket{qr, qg, qb, 0, std::max({r, g, b}) - std::min({r, g, b})};
        }
        it->second.count++;
    }
    if (buckets.empty()) return false;

    std::vector<Bucket> sorted;
    sorted.reserve(buckets.size());
    for (const auto& [key, bucket] : buckets) sorted.push_back(bucket);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Bucket& a, const Bucket& b) { return a.count > b.count; });

    // Dominant = most frequent color, darkened for background use
    const Bucket& dom = sorted[0];
    out->dominant = Color{ClampByte(dom.r * 0.5f), ClampByte(dom.g * 0.5f),
                          ClampByte(dom.b * 0.5f), 255};

    // Accent = most vivid color among the frequent ones
    const size_t topN = std::min<size_t>(10, sorted.size());
    const Bucket* accent = &sorted[0];
    for (size_t i = 1; i < topN; i++) {
        if (AccentScore(sorted[i]) > AccentScore(*accent)) accent = &sorted[i];
    }
    if (AccentScore(*accent) < 30.0f) {  // all washed out: search the full palette
        for (const Bucket& c : sorted) {
            if (AccentScore(c) > AccentScore(*accent)) accent = &c;
        }
    }
    out->accent = VibrantAccent(accent->r, accent->g, accent->b);

    // Secondary accent: best scoring color with a distinct hue from primary
    const float primaryHue = RgbToHue(static_cast<float>(accent->r),
                                      static_cast<float>(accent->g),
                                      static_cast<float>(accent->b));
    const Bucket* secondary = nullptr;
    float bestSecScore = 0;
    for (const Bucket& c : sorted) {
        if (&c == accent) continue;
        const float score = AccentScore(c);
        if (score <= bestSecScore) continue;
        if (HueDist(RgbToHue(static_cast<float>(c.r), static_cast<float>(c.g),
                             static_cast<float>(c.b)),
                    primaryHue) < kSecondaryHueThreshold) {
            continue;
        }
        secondary = &c;
        bestSecScore = score;
    }
    out->hasSecondary = secondary != nullptr && bestSecScore >= 20.0f;
    if (out->hasSecondary) {
        out->accentSecondary = VibrantAccent(secondary->r, secondary->g, secondary->b);
    }
    return true;
}
