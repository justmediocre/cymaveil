#pragma once

#include <cstddef>

// In-place iterative radix-2 FFT over interleaved complex data.
// n must be a power of two.
void Fft(float* re, float* im, size_t n);

// Convenience: windowed (Hann) power spectrum of n real samples.
// Writes n/2 linear magnitudes (normalized) into outMag.
void FftMagnitudes(const float* samples, size_t n, float* outMag);
