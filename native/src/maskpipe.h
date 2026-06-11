#pragma once

#include <vector>

// Depth-map -> foreground-alpha pipeline, ported from the web app's
// src/lib/segmentation/depthToMask.ts (same defaults, same stages):
//   1. 3x3 median filter        - removes salt-and-pepper depth noise
//   2. Bilateral filter         - smooths depth, preserves edges
//   3. Otsu threshold           - binary foreground/background split
//   4. Text/logo promotion      - recovers dense-edge regions from background
//   5. Edge-guided refinement   - snaps mask boundary to image edges
//   6. Morph close, then open   - fills holes, removes islands
//   7. Gaussian feather         - anti-aliases the final alpha
namespace maskpipe {

struct Params {
    int bilateralRadius = 4;
    float bilateralSigmaRange = 30.0f;
    int morphCloseRadius = 3;
    int morphOpenRadius = 2;
    int textPromotionRadius = 1;
    float textPromotionSensitivity = 50.0f;
    int edgeRefineRadius = 3;
    int featherRadius = 2;
};

// depth: w*h, higher = closer when foregroundIsHigh (Depth Anything outputs
// disparity, so pass true). rgba: the artwork resized to w*h, used for
// text promotion and edge refinement. Returns the w*h alpha map.
std::vector<unsigned char> DepthToMask(const std::vector<unsigned char>& depth,
                                       const unsigned char* rgba, int w, int h,
                                       bool foregroundIsHigh, const Params& p = {});

}  // namespace maskpipe
