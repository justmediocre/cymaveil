#pragma once

#include <string>
#include <vector>

// Edge-contour extraction for the "contour-bars" visualizer, ported from the
// web app's src/lib/edgeDetector.ts + src/lib/contourPath.ts: multi-pass
// Canny-like edges on a 128x128 downsample, connected components, the best
// mostly-horizontal contour (>= 60% of the width) smoothed and resampled to
// 24..80 points. Falls back to a gentle parabola when nothing qualifies.
// Points are normalized to 0..1 and every bar points straight up.
struct ContourPoint {
    float x, y;   // 0..1
    float nx, ny; // unit normal (always 0, -1)
};

struct ContourData {
    std::vector<ContourPoint> points;
    float span = 1.0f;  // horizontal extent 0..1 (scales bar length/width)
    bool fallback = true;
};

// Runs on the calling thread (a few ms for a 128x128 image). artPath empty
// or unreadable -> fallback contour.
ContourData ExtractContour(const std::string& artPath);
