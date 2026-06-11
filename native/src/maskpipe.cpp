#include "maskpipe.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

namespace maskpipe {
namespace {

using Buf = std::vector<unsigned char>;

int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

Buf MedianFilter(const Buf& src, int w, int h) {
    Buf out(static_cast<size_t>(w) * h);
    unsigned char win[9];
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int n = 0;
            for (int dy = -1; dy <= 1; dy++) {
                const int ny = ClampI(y + dy, 0, h - 1);
                for (int dx = -1; dx <= 1; dx++) {
                    const int nx = ClampI(x + dx, 0, w - 1);
                    win[n++] = src[ny * w + nx];
                }
            }
            std::nth_element(win, win + 4, win + 9);
            out[y * w + x] = win[4];
        }
    }
    return out;
}

Buf BilateralFilter(const Buf& src, int w, int h, int radius, float sigmaSpace,
                    float sigmaRange) {
    Buf out(static_cast<size_t>(w) * h);
    const float ss2 = 2.0f * sigmaSpace * sigmaSpace;
    const float sr2 = 2.0f * sigmaRange * sigmaRange;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int center = src[y * w + x];
            float sum = 0, wSum = 0;
            for (int dy = -radius; dy <= radius; dy++) {
                const int ny = ClampI(y + dy, 0, h - 1);
                for (int dx = -radius; dx <= radius; dx++) {
                    const int nx = ClampI(x + dx, 0, w - 1);
                    const int val = src[ny * w + nx];
                    const float dist2 = static_cast<float>(dx * dx + dy * dy);
                    const float diff = static_cast<float>(val - center);
                    const float weight = std::exp(-dist2 / ss2 - diff * diff / sr2);
                    sum += val * weight;
                    wSum += weight;
                }
            }
            out[y * w + x] = static_cast<unsigned char>(std::lround(sum / wSum));
        }
    }
    return out;
}

int OtsuThreshold(const Buf& data) {
    uint32_t histogram[256] = {};
    for (unsigned char v : data) histogram[v]++;
    const double total = static_cast<double>(data.size());
    double sumAll = 0;
    for (int i = 0; i < 256; i++) sumAll += i * static_cast<double>(histogram[i]);

    double sumBg = 0, weightBg = 0, maxVariance = 0;
    int best = 0;
    for (int t = 0; t < 256; t++) {
        weightBg += histogram[t];
        if (weightBg == 0) continue;
        const double weightFg = total - weightBg;
        if (weightFg == 0) break;
        sumBg += t * static_cast<double>(histogram[t]);
        const double meanBg = sumBg / weightBg;
        const double meanFg = (sumAll - sumBg) / weightFg;
        const double diff = meanBg - meanFg;
        const double variance = weightBg * weightFg * diff * diff;
        if (variance > maxVariance) {
            maxVariance = variance;
            best = t;
        }
    }
    return best;
}

void Dilate(Buf& mask, int w, int h, int r) {
    Buf out(mask.size());
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            bool found = false;
            for (int dy = -r; dy <= r && !found; dy++) {
                const int ny = y + dy;
                if (ny < 0 || ny >= h) continue;
                for (int dx = -r; dx <= r && !found; dx++) {
                    if (dx * dx + dy * dy > r * r) continue;
                    const int nx = x + dx;
                    if (nx < 0 || nx >= w) continue;
                    if (mask[ny * w + nx] > 0) found = true;
                }
            }
            out[y * w + x] = found ? 255 : 0;
        }
    }
    mask = std::move(out);
}

void Erode(Buf& mask, int w, int h, int r) {
    Buf out(mask.size());
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            bool allSet = true;
            for (int dy = -r; dy <= r && allSet; dy++) {
                const int ny = y + dy;
                if (ny < 0 || ny >= h) {
                    allSet = false;
                    continue;
                }
                for (int dx = -r; dx <= r && allSet; dx++) {
                    if (dx * dx + dy * dy > r * r) continue;
                    const int nx = x + dx;
                    if (nx < 0 || nx >= w) {
                        allSet = false;
                        continue;
                    }
                    if (mask[ny * w + nx] == 0) allSet = false;
                }
            }
            out[y * w + x] = allSet ? 255 : 0;
        }
    }
    mask = std::move(out);
}

// Sobel gradient magnitude over BT.601 luminance.
std::vector<float> ImageGradient(const unsigned char* rgba, int w, int h) {
    std::vector<float> lum(static_cast<size_t>(w) * h);
    for (int i = 0; i < w * h; i++) {
        lum[i] = 0.299f * rgba[i * 4] + 0.587f * rgba[i * 4 + 1] + 0.114f * rgba[i * 4 + 2];
    }
    std::vector<float> grad(static_cast<size_t>(w) * h, 0.0f);
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            const float gx = -lum[(y - 1) * w + x - 1] + lum[(y - 1) * w + x + 1] -
                             2 * lum[y * w + x - 1] + 2 * lum[y * w + x + 1] -
                             lum[(y + 1) * w + x - 1] + lum[(y + 1) * w + x + 1];
            const float gy = -lum[(y - 1) * w + x - 1] - 2 * lum[(y - 1) * w + x] -
                             lum[(y - 1) * w + x + 1] + lum[(y + 1) * w + x - 1] +
                             2 * lum[(y + 1) * w + x] + lum[(y + 1) * w + x + 1];
            grad[y * w + x] = std::sqrt(gx * gx + gy * gy);
        }
    }
    return grad;
}

// Histogram-based percentile (O(n)), as in the web implementation.
float HistogramPercentile(const std::vector<float>& data, float percentile) {
    if (data.empty()) return 0;
    float max = 0;
    for (float v : data) max = std::max(max, v);
    if (max == 0) return 0;
    constexpr int kBins = 1024;
    std::vector<uint32_t> histogram(kBins, 0);
    const float scale = (kBins - 1) / max;
    for (float v : data) histogram[std::min(static_cast<int>(v * scale), kBins - 1)]++;
    const size_t target = static_cast<size_t>(data.size() * percentile);
    size_t count = 0;
    for (int i = 0; i < kBins; i++) {
        count += histogram[i];
        if (count >= target) return (i + 0.5f) / scale;
    }
    return max;
}

void TextPromotion(Buf& alpha, const std::vector<float>& grad, int w, int h, int radius,
                   float sensitivity) {
    constexpr unsigned char kPromotionAlpha = 224;
    const int n = w * h;
    const float t = sensitivity / 100.0f;
    const float minDensity = 0.35f - t * 0.20f;
    const int minComponentSize = static_cast<int>(50 - t * 38);
    const float percentile = 0.97f - t * 0.27f;
    const float thresh = HistogramPercentile(grad, percentile);

    std::vector<uint8_t> edgeBin(n);
    for (int i = 0; i < n; i++) edgeBin[i] = grad[i] >= thresh ? 1 : 0;

    // Summed-area table over the binary edge map
    std::vector<int32_t> sat(n);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int i = y * w + x;
            sat[i] = edgeBin[i] + (x > 0 ? sat[i - 1] : 0) + (y > 0 ? sat[i - w] : 0) -
                     (x > 0 && y > 0 ? sat[i - w - 1] : 0);
        }
    }
    const auto rectSum = [&](int x0, int y0, int x1, int y1) {
        const int br = sat[y1 * w + x1];
        const int tl = (x0 > 0 && y0 > 0) ? sat[(y0 - 1) * w + x0 - 1] : 0;
        const int tr = (y0 > 0) ? sat[(y0 - 1) * w + x1] : 0;
        const int bl = (x0 > 0) ? sat[y1 * w + x0 - 1] : 0;
        return br - tr - bl + tl;
    };

    const int windowSize = 2 * radius + 1;
    const float windowArea = static_cast<float>(windowSize * windowSize);
    std::vector<uint8_t> candidates(n, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int i = y * w + x;
            if (alpha[i] != 0) continue;  // only promote background pixels
            const int x0 = std::max(x - radius, 0), y0 = std::max(y - radius, 0);
            const int x1 = std::min(x + radius, w - 1), y1 = std::min(y + radius, h - 1);
            if (rectSum(x0, y0, x1, y1) / windowArea >= minDensity) candidates[i] = 1;
        }
    }

    // Connected-component filter (4-connected BFS)
    std::vector<uint8_t> visited(n, 0);
    std::vector<int> component;
    std::deque<int> queue;
    for (int i = 0; i < n; i++) {
        if (candidates[i] != 1 || visited[i]) continue;
        component.clear();
        queue.clear();
        queue.push_back(i);
        visited[i] = 1;
        while (!queue.empty()) {
            const int ci = queue.front();
            queue.pop_front();
            component.push_back(ci);
            const int cx = ci % w, cy = ci / w;
            const int neighbors[4] = {cy > 0 ? ci - w : -1, cy < h - 1 ? ci + w : -1,
                                      cx > 0 ? ci - 1 : -1, cx < w - 1 ? ci + 1 : -1};
            for (int ni : neighbors) {
                if (ni >= 0 && !visited[ni] && candidates[ni] == 1) {
                    visited[ni] = 1;
                    queue.push_back(ni);
                }
            }
        }
        if (static_cast<int>(component.size()) >= minComponentSize) {
            for (int ci : component) alpha[ci] = kPromotionAlpha;
        }
    }
}

void EdgeRefine(Buf& alpha, const std::vector<float>& grad, int w, int h, int searchRadius) {
    const float edgeThresh = HistogramPercentile(grad, 0.85f);
    const Buf copy = alpha;
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            const int idx = y * w + x;
            const unsigned char val = copy[idx];
            if (copy[idx - 1] == val && copy[idx + 1] == val && copy[idx - w] == val &&
                copy[idx + w] == val) {
                continue;  // not a boundary pixel
            }
            float bestGrad = grad[idx];
            int bestX = x, bestY = y;
            for (int dy = -searchRadius; dy <= searchRadius; dy++) {
                const int ny = y + dy;
                if (ny < 1 || ny >= h - 1) continue;
                for (int dx = -searchRadius; dx <= searchRadius; dx++) {
                    const int nx = x + dx;
                    if (nx < 1 || nx >= w - 1) continue;
                    const float g = grad[ny * w + nx];
                    if (g > bestGrad && g >= edgeThresh) {
                        bestGrad = g;
                        bestX = nx;
                        bestY = ny;
                    }
                }
            }
            if (bestX != x || bestY != y) alpha[idx] = copy[bestY * w + bestX];
        }
    }
}

void GaussianFeather(Buf& alpha, int w, int h, int radius) {
    const int size = radius * 2 + 1;
    std::vector<double> kernel(size);
    const double sigma = radius / 2.0;
    for (int i = 0; i < size; i++) {
        const double x = i - radius;
        kernel[i] = std::exp(-(x * x) / (2 * sigma * sigma));
    }
    Buf tmp(alpha.size());
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double sum = 0, wSum = 0;
            for (int k = -radius; k <= radius; k++) {
                const int sx = ClampI(x + k, 0, w - 1);
                sum += alpha[y * w + sx] * kernel[k + radius];
                wSum += kernel[k + radius];
            }
            tmp[y * w + x] = static_cast<unsigned char>(std::lround(sum / wSum));
        }
    }
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            double sum = 0, wSum = 0;
            for (int k = -radius; k <= radius; k++) {
                const int sy = ClampI(y + k, 0, h - 1);
                sum += tmp[sy * w + x] * kernel[k + radius];
                wSum += kernel[k + radius];
            }
            alpha[y * w + x] = static_cast<unsigned char>(std::lround(sum / wSum));
        }
    }
}

}  // namespace

std::vector<unsigned char> DepthToMask(const std::vector<unsigned char>& depth,
                                       const unsigned char* rgba, int w, int h,
                                       bool foregroundIsHigh, const Params& p) {
    const Buf denoised = MedianFilter(depth, w, h);
    const Buf smoothed = BilateralFilter(denoised, w, h, p.bilateralRadius,
                                         p.bilateralRadius * 5.0f, p.bilateralSigmaRange);
    const int threshold = OtsuThreshold(smoothed);

    Buf alpha(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < smoothed.size(); i++) {
        const bool isForeground =
            foregroundIsHigh ? smoothed[i] >= threshold : smoothed[i] <= threshold;
        alpha[i] = isForeground ? 255 : 0;
    }

    if (p.textPromotionRadius > 0 || p.edgeRefineRadius > 0) {
        const std::vector<float> grad = ImageGradient(rgba, w, h);
        if (p.textPromotionRadius > 0) {
            TextPromotion(alpha, grad, w, h, p.textPromotionRadius, p.textPromotionSensitivity);
        }
        if (p.edgeRefineRadius > 0) EdgeRefine(alpha, grad, w, h, p.edgeRefineRadius);
    }

    if (p.morphCloseRadius > 0) {
        Dilate(alpha, w, h, p.morphCloseRadius);
        Erode(alpha, w, h, p.morphCloseRadius);
    }
    if (p.morphOpenRadius > 0) {
        Erode(alpha, w, h, p.morphOpenRadius);
        Dilate(alpha, w, h, p.morphOpenRadius);
    }
    if (p.featherRadius > 0) GaussianFeather(alpha, w, h, p.featherRadius);
    return alpha;
}

}  // namespace maskpipe
