#include "fft.h"

#include <cmath>
#include <vector>

void Fft(float* re, float* im, size_t n) {
    // Bit-reversal permutation
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * 3.14159265358979f / static_cast<float>(len);
        const float wr = std::cos(ang), wi = std::sin(ang);
        for (size_t i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (size_t k = 0; k < len / 2; k++) {
                const size_t a = i + k, b = i + k + len / 2;
                const float tr = re[b] * cr - im[b] * ci;
                const float ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
                const float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
}

void FftMagnitudes(const float* samples, size_t n, float* outMag) {
    std::vector<float> re(n), im(n, 0.0f);
    for (size_t i = 0; i < n; i++) {
        // Hann window
        const float w = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979f * static_cast<float>(i) / static_cast<float>(n - 1)));
        re[i] = samples[i] * w;
    }
    Fft(re.data(), im.data(), n);
    const float norm = 4.0f / static_cast<float>(n);  // 2/N for amplitude, 2x for window loss
    for (size_t i = 0; i < n / 2; i++) {
        outMag[i] = std::sqrt(re[i] * re[i] + im[i] * im[i]) * norm;
    }
}
