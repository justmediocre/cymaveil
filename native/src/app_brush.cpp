// Manual mask painting overlay (MaskBrushEditor.tsx): paint/erase the depth
// foreground for the cover on screen, with the live visualizer between the
// art and the mask preview so the effect can be judged while painting.
#include "app.h"

#include <algorithm>
#include <cmath>

#include "raymath.h"

#include "ui.h"

void App::OpenBrushEditor(const Art& art) {
    if (!art.Valid()) return;
    menu_.open = false;
    const std::string key = ArtKey(art.path);
    const std::string maskPath = DepthEngine::MaskPath(key);
    // Seed from the existing mask when there is one, else start from a blank
    // (all-background) canvas the user paints the in-front subject onto.
    if (!brush_.canvas.Init(art.path, DepthEngine::MaskExists(key) ? maskPath : "", 256)) {
        Toast("Couldn't load artwork");
        return;
    }
    brush_.art = art;
    if (brush_.artTex.id != 0) UnloadTexture(brush_.artTex);
    brush_.artTex = LoadTexture(art.path.c_str());
    SetTextureFilter(brush_.artTex, TEXTURE_FILTER_BILINEAR);
    brush_.overlayDirty = true;
    RebuildBrushOverlay();
    brush_.painting = false;
    brush_.open = true;
    MarkActivity();
}

void App::CloseBrushEditor() {
    brush_.open = false;
    brush_.painting = false;
    if (brush_.artTex.id != 0) {
        UnloadTexture(brush_.artTex);
        brush_.artTex = Texture2D{};
    }
    if (brush_.overlayTex.id != 0) {
        UnloadTexture(brush_.overlayTex);
        brush_.overlayTex = Texture2D{};
    }
    ShowCursor();
    MarkActivity();
}

void App::RebuildBrushOverlay() {
    const auto& alpha = brush_.canvas.Alpha();
    const auto& art = brush_.canvas.ArtRGBA();
    const int w = brush_.canvas.Width();
    const int h = brush_.canvas.Height();
    if (w == 0) return;
    brush_.overlayBuf.resize(static_cast<size_t>(w) * h * 4);
    for (int i = 0; i < w * h; i++) {
        const unsigned char r = art[i * 4];
        const unsigned char g = art[i * 4 + 1];
        const unsigned char b = art[i * 4 + 2];
        unsigned char* o = &brush_.overlayBuf[i * 4];
        if (alpha[i] > 128) {  // foreground: opaque art, sits in front of the bars
            o[0] = r;
            o[1] = g;
            o[2] = b;
            o[3] = 255;
        } else {  // background: dim + translucent so the visualizer shows through
            o[0] = static_cast<unsigned char>(r * 0.30f);
            o[1] = static_cast<unsigned char>(g * 0.25f);
            o[2] = static_cast<unsigned char>(b * 0.40f);
            o[3] = 90;
        }
    }
    if (brush_.overlayTex.id == 0) {
        Image img{brush_.overlayBuf.data(), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        brush_.overlayTex = LoadTextureFromImage(img);
        SetTextureFilter(brush_.overlayTex, TEXTURE_FILTER_BILINEAR);
    } else {
        UpdateTexture(brush_.overlayTex, brush_.overlayBuf.data());
    }
    brush_.overlayDirty = false;
}

void App::SaveBrushMask() {
    const std::string key = ArtKey(brush_.art.path);
    if (!brush_.canvas.Save(DepthEngine::MaskPath(key))) {
        Toast("Couldn't save mask");
        return;
    }
    // A hand-painted mask is only visible with depth layers on; turn them on so
    // the work shows immediately rather than silently doing nothing.
    if (!config_.depthLayers) {
        config_.depthLayers = true;
        config_.Save();
    }
    fg_.artKey.clear();
    fg_.checkedKey.clear();
    BuildForeground(brush_.art);
    Toast("Mask saved");
    CloseBrushEditor();
}

void App::DrawBrushEditor(Rectangle r) {
    if (brush_.overlayDirty) RebuildBrushOverlay();

    // Opaque backdrop covering the chrome underneath.
    DrawRectangleRec(r, ui::theme.bg);
    const Color accent = brush_.art.accent;

    // ── Geometry: a centred square canvas with the toolbar below it ──
    const float toolbarH = 46, gap = 18;
    const float canvasSize = std::min({720.0f, r.width - 120, r.height - 200});
    const float cx = r.x + r.width / 2;
    const float blockH = canvasSize + gap + toolbarH;
    const float top = r.y + std::max(48.0f, (r.height - blockH) / 2);
    const Rectangle artRect{cx - canvasSize / 2, top, canvasSize, canvasSize};

    ui::TextCentered("Paint the foreground — bars play behind whatever you paint",
                     Vector2{cx, std::max(r.y + 22, top - 24)}, 14, ui::theme.textSecondary);

    // ── Layer 0: album art ──
    if (brush_.artTex.id != 0) {
        ui::RoundedTexture(brush_.artTex, ui::CoverSrc(brush_.artTex), artRect, 16, WHITE);
    }
    // ── Layer 1: visualizer, between the art and the mask (same insets as NP) ──
    BeginScissorMode(static_cast<int>(artRect.x), static_cast<int>(artRect.y), static_cast<int>(artRect.width),
                     static_cast<int>(artRect.height));
    if (config_.canvasVisualizer && player_.IsPlaying()) {
        const Visualizer::FrameStyle fs = Visualizer::ComputeFrameStyle(
            config_.visualizerIntensity / 100.0f, accent, brush_.art.hasSecondary ? &brush_.art.accentSecondary : nullptr,
            true);
        const Visualizer::Style style = ResolvedStyle();
        visualizer_.Draw(style, artRect, fs, style == Visualizer::Style::ContourBars ? &contour_.data : nullptr);
    }
    EndScissorMode();
    // ── Layer 2: mask preview composite ──
    if (brush_.overlayTex.id != 0) {
        ui::RoundedTexture(brush_.overlayTex,
                           Rectangle{0, 0, static_cast<float>(brush_.overlayTex.width), static_cast<float>(brush_.overlayTex.height)},
                           artRect, 16, WHITE);
    }
    ui::RoundedRectLines(artRect, 16, 1, Fade(ui::theme.text, 0.08f));

    // ── Painting: mouse → mask-space, with brush stroke interpolation ──
    const Vector2 m = GetMousePosition();
    const bool overCanvas = !menu_.open && CheckCollisionPointRec(m, artRect);
    const float toMask = brush_.canvas.Width() / artRect.width;  // px → mask units
    const float mx = (m.x - artRect.x) * toMask;
    const float my = (m.y - artRect.y) * toMask;
    if (overCanvas && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        brush_.canvas.BeginStroke();
        brush_.canvas.PaintPoint(mx, my);
        brush_.lastMask = Vector2{mx, my};
        brush_.painting = true;
        brush_.overlayDirty = true;
        MarkActivity();
    } else if (brush_.painting && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        brush_.canvas.PaintLine(brush_.lastMask.x, brush_.lastMask.y, mx, my);
        brush_.lastMask = Vector2{mx, my};
        brush_.overlayDirty = true;
    } else if (brush_.painting && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        brush_.canvas.CommitStroke();
        brush_.painting = false;
    }

    // Hide the OS cursor over the canvas and draw our own brush ring instead.
    if (overCanvas) {
        HideCursor();
        const float rad = brush_.canvas.Radius() / toMask;
        const Color ring = brush_.canvas.mode() == BrushCanvas::Mode::Paint ? Color{255, 255, 255, 200}
                                                                            : Color{255, 90, 90, 220};
        DrawRing(m, std::max(0.5f, rad - 0.75f), rad + 0.75f, 0, 360, 64, ring);
        DrawLineEx(Vector2{m.x - 4, m.y}, Vector2{m.x + 4, m.y}, 1, Fade(ring, 0.7f));
        DrawLineEx(Vector2{m.x, m.y - 4}, Vector2{m.x, m.y + 4}, 1, Fade(ring, 0.7f));
    } else {
        ShowCursor();
    }

    // ── Keyboard: mode, size, undo/redo, save, close ──
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (ui::KeyPressed(KEY_X)) brush_.canvas.ToggleMode();
    if (IsKeyPressed(KEY_LEFT_BRACKET)) brush_.canvas.SetRadius(brush_.canvas.Radius() - 1);
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) brush_.canvas.SetRadius(brush_.canvas.Radius() + 1);
    if (ctrl && GetMouseWheelMove() != 0) {
        brush_.canvas.SetRadius(brush_.canvas.Radius() + (GetMouseWheelMove() > 0 ? 1 : -1));
    }
    if (ctrl && !shift && IsKeyPressed(KEY_Z)) {
        brush_.canvas.Undo();
        brush_.overlayDirty = true;
    }
    if (ctrl && (IsKeyPressed(KEY_Y) || (shift && IsKeyPressed(KEY_Z)))) {
        brush_.canvas.Redo();
        brush_.overlayDirty = true;
    }
    if (ctrl && IsKeyPressed(KEY_S)) {
        SaveBrushMask();
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        CloseBrushEditor();
        return;
    }

    // ── Toolbar: glass pill below the canvas ──
    const float bh = 30, padIn = 14, g = 8;
    const float wPaint = 58, wErase = 58, wMinus = 26, wSize = 66, wPlus = 26;
    const float wUndo = 54, wRedo = 54, wReset = 58, wSave = 68, wClose = 30;
    const float widths[] = {wPaint, wErase, wMinus, wSize, wPlus, wUndo, wRedo, wReset, wSave, wClose};
    constexpr int kNumItems = 10;
    float total = 2 * padIn + g * (kNumItems - 1);
    for (float w : widths) total += w;
    const float ty = artRect.y + canvasSize + gap;
    const Rectangle panel{cx - total / 2, ty, total, toolbarH};
    ui::RoundedRect(panel, 23, ui::theme.surface);
    ui::RoundedRectLines(panel, 23, 1, ui::theme.borderSubtle);

    float bx = panel.x + padIn;
    const float by = ty + (toolbarH - bh) / 2;
    const auto place = [&](float w) {
        const Rectangle b{bx, by, w, bh};
        bx += w + g;
        return b;
    };
    const auto button = [&](Rectangle b, const char* label, bool active, bool enabled) {
        const bool hov = enabled && ui::HoverRaw(b);
        if (active) {
            ui::RoundedRect(b, 10, ui::theme.accentDim);
            ui::RoundedRectLines(b, 10, 1, accent);
        } else if (hov) {
            ui::RoundedRect(b, 10, ui::theme.elevated);
        }
        const Color col = !enabled ? ui::theme.textTertiary : active ? accent : hov ? ui::theme.text : ui::theme.textSecondary;
        ui::TextCentered(label, Vector2{b.x + b.width / 2, b.y + b.height / 2}, 13, col, ui::Face::SansMedium);
        return enabled && ui::ClickedRaw(b);
    };

    const bool isPaint = brush_.canvas.mode() == BrushCanvas::Mode::Paint;
    if (button(place(wPaint), "Paint", isPaint, true)) brush_.canvas.SetMode(BrushCanvas::Mode::Paint);
    if (button(place(wErase), "Erase", !isPaint, true)) brush_.canvas.SetMode(BrushCanvas::Mode::Erase);
    if (button(place(wMinus), "\xE2\x88\x92", false, true)) brush_.canvas.SetRadius(brush_.canvas.Radius() - 1);
    {
        const Rectangle b = place(wSize);
        ui::TextCentered(TextFormat("Size %d", brush_.canvas.Radius()), Vector2{b.x + b.width / 2, b.y + b.height / 2}, 13,
                         ui::theme.text, ui::Face::Mono);
    }
    if (button(place(wPlus), "+", false, true)) brush_.canvas.SetRadius(brush_.canvas.Radius() + 1);
    if (button(place(wUndo), "Undo", false, brush_.canvas.CanUndo())) {
        brush_.canvas.Undo();
        brush_.overlayDirty = true;
    }
    if (button(place(wRedo), "Redo", false, brush_.canvas.CanRedo())) {
        brush_.canvas.Redo();
        brush_.overlayDirty = true;
    }
    if (button(place(wReset), "Reset", false, true)) {
        brush_.canvas.Reset();
        brush_.overlayDirty = true;
    }
    {
        const Rectangle b = place(wSave);
        const bool hov = ui::HoverRaw(b);
        ui::RoundedRect(b, 10, hov ? ui::Mix(accent, WHITE, 0.12f) : accent);
        ui::TextCentered("Save", Vector2{b.x + b.width / 2, b.y + b.height / 2}, 13, ui::theme.bg, ui::Face::SansMedium);
        if (ui::ClickedRaw(b)) {
            SaveBrushMask();
            return;
        }
    }
    {
        const Rectangle b = place(wClose);
        const bool hov = ui::HoverRaw(b);
        if (hov) ui::RoundedRect(b, 10, ui::theme.elevated);
        ui::IconClose(Vector2{b.x + b.width / 2, b.y + b.height / 2}, 14, hov ? ui::theme.text : ui::theme.textSecondary, 2.0f);
        if (ui::ClickedRaw(b)) {
            CloseBrushEditor();
            return;
        }
    }

    ui::TextCentered("X paint/erase    [ ] size    Ctrl+Scroll size    Ctrl+Z/Y undo    Ctrl+S save    Esc close",
                     Vector2{cx, ty + toolbarH + 18}, 11, ui::theme.textTertiary);
}
