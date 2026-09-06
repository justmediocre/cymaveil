#include "contour.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "raylib.h"

namespace {

constexpr int kDetectSize = 128;
constexpr int kMargin = 5;
constexpr int kMinPoints = 24;
constexpr int kMaxPoints = 80;
constexpr float kPointsPerUnitLength = 200.0f;
constexpr float kMinHSpan = 0.6f;
constexpr int kSmoothWindow = 9;

struct Pt {
    float x, y;
};

struct Component {
    std::vector<Pt> pixels;
    int minX, maxX, minY, maxY;
    float span = 0, score = 0;
};

std::vector<float> GaussianBlur5x5(const std::vector<float>& src, int w, int h) {
    static const float kernel[25] = {1, 4, 7, 4, 1, 4, 16, 26, 16, 4, 7, 26, 41, 26, 7,
                                     4, 16, 26, 16, 4, 1, 4, 7, 4, 1};
    std::vector<float> out(static_cast<size_t>(w) * h, 0.0f);
    for (int y = 2; y < h - 2; y++) {
        for (int x = 2; x < w - 2; x++) {
            float sum = 0;
            int ki = 0;
            for (int ky = -2; ky <= 2; ky++)
                for (int kx = -2; kx <= 2; kx++) sum += src[(y + ky) * w + (x + kx)] * kernel[ki++];
            out[y * w + x] = sum / 273.0f;
        }
    }
    return out;
}

std::vector<unsigned char> RunPipeline(const std::vector<float>& blurred, int w, int h,
                                       float highMul, float lowMul) {
    std::vector<float> mag(static_cast<size_t>(w) * h, 0.0f), dir(static_cast<size_t>(w) * h, 0.0f);
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            const float tl = blurred[(y - 1) * w + (x - 1)], t = blurred[(y - 1) * w + x],
                        tr = blurred[(y - 1) * w + (x + 1)], l = blurred[y * w + (x - 1)],
                        r = blurred[y * w + (x + 1)], bl = blurred[(y + 1) * w + (x - 1)],
                        b = blurred[(y + 1) * w + x], br = blurred[(y + 1) * w + (x + 1)];
            const float gx = -tl + tr - 2 * l + 2 * r - bl + br;
            const float gy = -tl - 2 * t - tr + bl + 2 * b + br;
            mag[y * w + x] = std::sqrt(gx * gx + gy * gy);
            dir[y * w + x] = std::atan2(gy, gx);
        }
    }
    std::vector<float> nms(static_cast<size_t>(w) * h, 0.0f);
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            const int idx = y * w + x;
            const float angle = std::fmod(dir[idx] * 180.0f / PI + 180.0f, 180.0f);
            float n1, n2;
            if (angle < 22.5f || angle >= 157.5f) {
                n1 = mag[y * w + (x - 1)];
                n2 = mag[y * w + (x + 1)];
            } else if (angle < 67.5f) {
                n1 = mag[(y - 1) * w + (x + 1)];
                n2 = mag[(y + 1) * w + (x - 1)];
            } else if (angle < 112.5f) {
                n1 = mag[(y - 1) * w + x];
                n2 = mag[(y + 1) * w + x];
            } else {
                n1 = mag[(y - 1) * w + (x - 1)];
                n2 = mag[(y + 1) * w + (x + 1)];
            }
            nms[idx] = (mag[idx] >= n1 && mag[idx] >= n2) ? mag[idx] : 0.0f;
        }
    }
    float maxMag = 0;
    for (float v : nms) maxMag = std::max(maxMag, v);
    std::vector<unsigned char> edges(static_cast<size_t>(w) * h, 0);
    if (maxMag == 0) return edges;
    const float high = maxMag * highMul, low = maxMag * lowMul;
    std::vector<int> stack;
    for (int i = 0; i < w * h; i++) {
        if (nms[i] >= high) {
            edges[i] = 255;
            stack.push_back(i);
        }
    }
    while (!stack.empty()) {
        const int idx = stack.back();
        stack.pop_back();
        const int x = idx % w, y = idx / w;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                const int ni = ny * w + nx;
                if (edges[ni] == 0 && nms[ni] >= low) {
                    edges[ni] = 255;
                    stack.push_back(ni);
                }
            }
        }
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (x < kMargin || x >= w - kMargin || y < kMargin || y >= h - kMargin) edges[y * w + x] = 0;
    return edges;
}

std::vector<Component> LabelComponents(const std::vector<unsigned char>& edges, int w, int h) {
    std::vector<unsigned char> visited(static_cast<size_t>(w) * h, 0);
    std::vector<Component> comps;
    std::vector<int> stack;
    for (int i = 0; i < w * h; i++) {
        if (edges[i] == 0 || visited[i]) continue;
        Component c{{}, w, 0, h, 0};
        stack.assign(1, i);
        visited[i] = 1;
        while (!stack.empty()) {
            const int idx = stack.back();
            stack.pop_back();
            const int x = idx % w, y = idx / w;
            c.pixels.push_back({static_cast<float>(x), static_cast<float>(y)});
            c.minX = std::min(c.minX, x);
            c.maxX = std::max(c.maxX, x);
            c.minY = std::min(c.minY, y);
            c.maxY = std::max(c.maxY, y);
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                    const int ni = ny * w + nx;
                    if (edges[ni] != 0 && !visited[ni]) {
                        visited[ni] = 1;
                        stack.push_back(ni);
                    }
                }
            }
        }
        comps.push_back(std::move(c));
    }
    return comps;
}

std::vector<Pt> OrderPixels(const std::vector<Pt>& pixels) {
    if (pixels.size() <= 1) return pixels;
    const int gridSize = 4;
    std::unordered_map<long long, std::vector<int>> grid;
    auto key = [](int gx, int gy) { return (static_cast<long long>(gx) << 32) ^ (gy & 0xffffffffLL); };
    for (size_t i = 0; i < pixels.size(); i++) {
        grid[key(static_cast<int>(pixels[i].x) / gridSize, static_cast<int>(pixels[i].y) / gridSize)]
            .push_back(static_cast<int>(i));
    }
    std::vector<unsigned char> used(pixels.size(), 0);
    std::vector<Pt> ordered;
    size_t start = 0;
    for (size_t i = 1; i < pixels.size(); i++) {
        if (pixels[i].y < pixels[start].y ||
            (pixels[i].y == pixels[start].y && pixels[i].x < pixels[start].x))
            start = i;
    }
    used[start] = 1;
    ordered.push_back(pixels[start]);
    while (ordered.size() < pixels.size()) {
        const Pt cur = ordered.back();
        const int cxg = static_cast<int>(cur.x) / gridSize, cyg = static_cast<int>(cur.y) / gridSize;
        int best = -1;
        float bestDist = 1e30f;
        for (int gy = cyg - 2; gy <= cyg + 2; gy++) {
            for (int gx = cxg - 2; gx <= cxg + 2; gx++) {
                auto it = grid.find(key(gx, gy));
                if (it == grid.end()) continue;
                for (int idx : it->second) {
                    if (used[idx]) continue;
                    const float dx = pixels[idx].x - cur.x, dy = pixels[idx].y - cur.y;
                    const float d = dx * dx + dy * dy;
                    if (d < bestDist) {
                        bestDist = d;
                        best = idx;
                    }
                }
            }
        }
        if (best == -1 || bestDist > 36) {
            bestDist = 1e30f;
            for (size_t i = 0; i < pixels.size(); i++) {
                if (used[i]) continue;
                const float dx = pixels[i].x - cur.x, dy = pixels[i].y - cur.y;
                const float d = dx * dx + dy * dy;
                if (d < bestDist) {
                    bestDist = d;
                    best = static_cast<int>(i);
                }
            }
        }
        if (best == -1 || bestDist > 64) break;
        used[best] = 1;
        ordered.push_back(pixels[best]);
    }
    return ordered;
}

std::vector<Pt> SmoothPath(const std::vector<Pt>& pts, int window) {
    const int half = window / 2;
    std::vector<Pt> out(pts.size());
    const int n = static_cast<int>(pts.size());
    for (int i = 0; i < n; i++) {
        float sx = 0, sy = 0;
        int count = 0;
        for (int j = -half; j <= half; j++) {
            const int idx = std::clamp(i + j, 0, n - 1);
            sx += pts[idx].x;
            sy += pts[idx].y;
            count++;
        }
        out[i] = {sx / count, sy / count};
    }
    return out;
}

float ArcLength(const std::vector<Pt>& pts) {
    float len = 0;
    for (size_t i = 1; i < pts.size(); i++) {
        const float dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y;
        len += std::sqrt(dx * dx + dy * dy);
    }
    return len;
}

std::vector<Pt> Resample(const std::vector<Pt>& pts, int n) {
    if (pts.size() < 2) return pts;
    std::vector<float> arc(pts.size(), 0.0f);
    for (size_t i = 1; i < pts.size(); i++) {
        const float dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y;
        arc[i] = arc[i - 1] + std::sqrt(dx * dx + dy * dy);
    }
    const float total = arc.back();
    std::vector<Pt> out;
    if (total == 0) {
        out.assign(pts.begin(), pts.begin() + std::min<size_t>(n, pts.size()));
        return out;
    }
    size_t src = 0;
    for (int i = 0; i < n; i++) {
        const float target = static_cast<float>(i) / (n - 1) * total;
        while (src < arc.size() - 1 && arc[src + 1] < target) src++;
        if (src >= pts.size() - 1) {
            out.push_back(pts.back());
            continue;
        }
        const float seg = arc[src + 1] - arc[src];
        const float t = seg > 0 ? (target - arc[src]) / seg : 0;
        out.push_back({pts[src].x + t * (pts[src + 1].x - pts[src].x),
                       pts[src].y + t * (pts[src + 1].y - pts[src].y)});
    }
    return out;
}

ContourData Fallback() {
    ContourData d;
    d.fallback = true;
    d.span = 1.0f;
    const int n = 48;
    for (int i = 0; i < n; i++) {
        const float t = static_cast<float>(i) / (n - 1);
        d.points.push_back({t, 0.92f - 0.32f * t * (1 - t), 0, -1});
    }
    return d;
}

}  // namespace

ContourData ExtractContour(const std::string& artPath) {
    if (artPath.empty()) return Fallback();
    Image img = LoadImage(artPath.c_str());
    if (img.data == nullptr || img.width <= 0 || img.height <= 0) {
        UnloadImage(img);
        return Fallback();
    }
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    // object-fit: the web draws the (square) art scaled to 128x128.
    ImageResize(&img, kDetectSize, kDetectSize);
    const int w = kDetectSize, h = kDetectSize;
    std::vector<float> gray(static_cast<size_t>(w) * h);
    const auto* px = static_cast<const unsigned char*>(img.data);
    for (int i = 0; i < w * h; i++) {
        gray[i] = 0.299f * px[i * 4] + 0.587f * px[i * 4 + 1] + 0.114f * px[i * 4 + 2];
    }
    UnloadImage(img);

    const auto blur1 = GaussianBlur5x5(gray, w, h);
    const auto blur2 = GaussianBlur5x5(blur1, w, h);
    const std::vector<std::vector<unsigned char>> passes{
        RunPipeline(blur2, w, h, 0.35f, 0.15f),
        RunPipeline(blur1, w, h, 0.25f, 0.10f),
        RunPipeline(blur1, w, h, 0.15f, 0.06f),
    };

    std::vector<Component> all;
    for (const auto& edges : passes) {
        auto comps = LabelComponents(edges, w, h);
        for (auto& c : comps) all.push_back(std::move(c));
    }
    if (all.empty()) return Fallback();

    const float imgDiag = std::sqrt(static_cast<float>(w * w + h * h));
    const float cx = w / 2.0f, cy = h / 2.0f, maxCenterDist = imgDiag / 2;
    const Component* best = nullptr;
    for (auto& c : all) {
        c.span = static_cast<float>(c.maxX - c.minX) / w;
        const float comX = (c.minX + c.maxX) / 2.0f, comY = (c.minY + c.maxY) / 2.0f;
        const float dist = std::sqrt((comX - cx) * (comX - cx) + (comY - cy) * (comY - cy));
        c.score = c.span * (1.0f - 0.5f * (dist / maxCenterDist));
        if (c.span >= kMinHSpan && c.pixels.size() >= 10 && (best == nullptr || c.score > best->score)) {
            best = &c;
        }
    }
    if (best == nullptr) return Fallback();

    const auto ordered = OrderPixels(best->pixels);
    const auto smoothed = SmoothPath(ordered, kSmoothWindow);
    const float normalizedArcLen = ArcLength(smoothed) / imgDiag;
    const int numPoints = std::clamp(static_cast<int>(std::lround(normalizedArcLen * kPointsPerUnitLength)),
                                     kMinPoints, kMaxPoints);
    const auto resampled = Resample(smoothed, numPoints);

    ContourData d;
    d.fallback = false;
    d.span = best->span;
    for (const auto& p : resampled) d.points.push_back({p.x / w, p.y / h, 0, -1});
    return d;
}
