#include "brush.h"

#include <algorithm>
#include <cmath>

#include "raylib.h"

namespace {
constexpr int kMaxUndo = 30;
constexpr int kMinRadius = 1;
constexpr int kMaxRadius = 80;

// Copies a raylib Image's pixels into `out`, reformatting + resizing first.
// Returns false when the image failed to load.
bool LoadResized(const std::string& path, int size, PixelFormat fmt, int channels,
                 std::vector<unsigned char>& out) {
    Image img = LoadImage(path.c_str());
    if (img.data == nullptr) return false;
    ImageFormat(&img, fmt);
    ImageResize(&img, size, size);
    const auto* px = static_cast<const unsigned char*>(img.data);
    out.assign(px, px + static_cast<size_t>(size) * size * channels);
    UnloadImage(img);
    return true;
}
}  // namespace

bool BrushCanvas::Init(const std::string& artPath, const std::string& maskPath, int size) {
    if (!LoadResized(artPath, size, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 4, art_)) return false;

    width_ = height_ = size;
    // Blank canvas is all background; the user paints the in-front subject.
    alpha_.assign(static_cast<size_t>(size) * size, 0);
    if (!maskPath.empty()) {
        LoadResized(maskPath, size, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, 1, alpha_);
    }

    seed_ = alpha_;
    radius_ = 12;
    mode_ = Mode::Paint;
    undo_.clear();
    redo_.clear();
    return true;
}

void BrushCanvas::SetRadius(int r) { radius_ = std::clamp(r, kMinRadius, kMaxRadius); }

void BrushCanvas::PaintPoint(float x, float y) {
    const unsigned char value = mode_ == Mode::Paint ? 255 : 0;
    const float r = static_cast<float>(radius_);
    const float r2 = r * r;

    const int x0 = std::max(0, static_cast<int>(std::floor(x - r)));
    const int y0 = std::max(0, static_cast<int>(std::floor(y - r)));
    const int x1 = std::min(width_ - 1, static_cast<int>(std::ceil(x + r)));
    const int y1 = std::min(height_ - 1, static_cast<int>(std::ceil(y + r)));

    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            const float dx = px - x;
            const float dy = py - y;
            if (dx * dx + dy * dy <= r2) alpha_[static_cast<size_t>(py) * width_ + px] = value;
        }
    }
}

void BrushCanvas::PaintLine(float x0, float y0, float x1, float y1) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float spacing = std::max(1.0f, radius_ / 2.0f);
    const int steps = std::max(1, static_cast<int>(std::ceil(dist / spacing)));

    for (int i = 0; i <= steps; i++) {
        const float t = static_cast<float>(i) / steps;
        PaintPoint(x0 + dx * t, y0 + dy * t);
    }
}

void BrushCanvas::BeginStroke() { strokeStart_ = alpha_; }

void BrushCanvas::CommitStroke() {
    if (strokeStart_.empty() || strokeStart_ == alpha_) return;  // nothing changed
    undo_.push_back(std::move(strokeStart_));
    strokeStart_.clear();
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    redo_.clear();
}

void BrushCanvas::Undo() {
    if (undo_.empty()) return;
    redo_.push_back(alpha_);
    alpha_ = std::move(undo_.back());
    undo_.pop_back();
}

void BrushCanvas::Redo() {
    if (redo_.empty()) return;
    undo_.push_back(alpha_);
    alpha_ = std::move(redo_.back());
    redo_.pop_back();
}

void BrushCanvas::Reset() {
    alpha_ = seed_;
    undo_.clear();
    redo_.clear();
}

bool BrushCanvas::Save(const std::string& path) const {
    std::vector<unsigned char> buf = alpha_;  // ExportImage wants a non-const buffer
    Image img{buf.data(), width_, height_, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE};
    return ExportImage(img, path.c_str());
}
