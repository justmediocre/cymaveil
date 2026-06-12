#pragma once

#include <string>
#include <vector>

// Manual foreground-mask painting, ported from the web app's
// src/lib/brushEngine.ts. Holds a w*h 8-bit alpha map (255 = foreground, drawn
// in front of the visualizer; 0 = background, the bars show through), a
// circular brush, and undo/redo stacks. The album art RGBA is kept alongside
// so the editor can composite a live preview. All coordinates are mask-space.
class BrushCanvas {
public:
    enum class Mode { Paint, Erase };

    // Seeds the alpha map from an existing grayscale mask PNG when maskPath is
    // non-empty and loadable, otherwise starts blank (all background). The art
    // is resized to size*size for the preview composite. Returns false only if
    // the artwork itself can't be loaded.
    bool Init(const std::string& artPath, const std::string& maskPath, int size);
    bool Valid() const { return width_ > 0; }

    int Width() const { return width_; }
    int Height() const { return height_; }
    int Radius() const { return radius_; }
    void SetRadius(int r);
    Mode mode() const { return mode_; }
    void SetMode(Mode m) { mode_ = m; }
    void ToggleMode() { mode_ = mode_ == Mode::Paint ? Mode::Erase : Mode::Paint; }

    // Stamp a circular brush, or a stroke interpolated between two points.
    void PaintPoint(float x, float y);
    void PaintLine(float x0, float y0, float x1, float y1);

    // Bracket a stroke: BeginStroke snapshots the pre-stroke alpha, CommitStroke
    // pushes that snapshot onto the undo stack (only if anything changed).
    void BeginStroke();
    void CommitStroke();

    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    void Undo();
    void Redo();
    void Reset();  // back to the seeded state, clearing history

    const std::vector<unsigned char>& Alpha() const { return alpha_; }
    const std::vector<unsigned char>& ArtRGBA() const { return art_; }

    // Writes the alpha map as a grayscale PNG (the format BuildForeground reads).
    bool Save(const std::string& path) const;

private:
    int width_ = 0;
    int height_ = 0;
    int radius_ = 12;
    Mode mode_ = Mode::Paint;
    std::vector<unsigned char> alpha_;  // w*h
    std::vector<unsigned char> art_;    // w*h*4 RGBA
    std::vector<unsigned char> seed_;   // initial alpha, for Reset
    std::vector<std::vector<unsigned char>> undo_;
    std::vector<std::vector<unsigned char>> redo_;
    std::vector<unsigned char> strokeStart_;  // pre-stroke snapshot
};
