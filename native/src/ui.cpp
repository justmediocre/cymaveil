#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "raymath.h"

namespace ui {

Theme theme;

namespace {

Font g_font{};
bool g_fontLoaded = false;

// One font atlas rendered at high size, scaled down at draw time (bilinear).
constexpr int kAtlasSize = 48;

const char* kFontCandidates[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",       // debian/ubuntu (dev container)
    "/usr/share/fonts/TTF/DejaVuSans.ttf",                   // arch
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",            // arch noto
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",   // debian noto
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
};

std::vector<int> Codepoints() {
    std::vector<int> cps;
    for (int c = 32; c <= 0x17F; c++) cps.push_back(c);   // ASCII + Latin-1 + Latin Extended-A
    for (int c = 0x2010; c <= 0x2027; c++) cps.push_back(c);  // dashes, quotes
    cps.push_back(0x2030);
    cps.push_back(0x2122);  // ™
    return cps;
}

Font F() { return g_fontLoaded ? g_font : GetFontDefault(); }
float Spacing(float size) { return g_fontLoaded ? 0.0f : size / 10.0f; }

// DrawTriangle culls based on winding; draw both windings so icon geometry
// never silently disappears.
void Tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    DrawTriangle(a, b, c, col);
    DrawTriangle(a, c, b, col);
}

}  // namespace

void Init() {
    auto cps = Codepoints();
    for (const char* path : kFontCandidates) {
        if (std::filesystem::exists(path)) {
            g_font = LoadFontEx(path, kAtlasSize, cps.data(), static_cast<int>(cps.size()));
            if (g_font.texture.id != 0) {
                SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
                g_fontLoaded = true;
                break;
            }
        }
    }
}

void Shutdown() {
    if (g_fontLoaded) UnloadFont(g_font);
    g_fontLoaded = false;
}

void Text(const std::string& s, Vector2 pos, float size, Color c) {
    DrawTextEx(F(), s.c_str(), pos, size, Spacing(size), c);
}

Vector2 Measure(const std::string& s, float size) {
    return MeasureTextEx(F(), s.c_str(), size, Spacing(size));
}

void TextEllipsis(const std::string& s, Vector2 pos, float maxWidth, float size, Color c) {
    if (Measure(s, size).x <= maxWidth) {
        Text(s, pos, size, c);
        return;
    }
    std::string cut = s;
    while (!cut.empty()) {
        // Drop one UTF-8 codepoint from the end
        size_t i = cut.size() - 1;
        while (i > 0 && (static_cast<unsigned char>(cut[i]) & 0xC0) == 0x80) i--;
        cut.erase(i);
        if (Measure(cut + "…", size).x <= maxWidth) break;
    }
    Text(cut + "…", pos, size, c);
}

void TextRight(const std::string& s, Vector2 posRight, float size, Color c) {
    const float w = Measure(s, size).x;
    Text(s, Vector2{posRight.x - w, posRight.y}, size, c);
}

void TextCentered(const std::string& s, Vector2 center, float size, Color c) {
    const Vector2 m = Measure(s, size);
    Text(s, Vector2{center.x - m.x / 2, center.y - m.y / 2}, size, c);
}

bool Hover(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }

bool Clicked(Rectangle r) {
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && Hover(r);
}

bool Slider(Rectangle r, float* value, bool* dragging) {
    // Generous vertical hit area for slim bars
    Rectangle hit{r.x, r.y - 6, r.width, r.height + 12};
    if (Clicked(hit)) *dragging = true;
    if (!*dragging) return false;
    const float nv = Clamp((GetMousePosition().x - r.x) / r.width, 0.0f, 1.0f);
    const bool changed = nv != *value;
    *value = nv;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) *dragging = false;
    return changed || *dragging;
}

void ScrollArea(Rectangle view, float contentHeight, float* scroll) {
    if (Hover(view)) {
        *scroll -= GetMouseWheelMove() * 96.0f;
    }
    const float maxScroll = std::max(0.0f, contentHeight - view.height);
    *scroll = Clamp(*scroll, 0.0f, maxScroll);
    if (maxScroll > 0) {
        const float thumbH = std::max(32.0f, view.height * view.height / contentHeight);
        const float thumbY = view.y + (*scroll / maxScroll) * (view.height - thumbH);
        DrawRectangleRounded(Rectangle{view.x + view.width - 5, thumbY, 3, thumbH}, 1.0f, 4,
                             theme.border);
    }
}

void IconPlay(Vector2 c, float s, Color col) {
    Tri(Vector2{c.x - s * 0.32f, c.y - s * 0.45f}, Vector2{c.x - s * 0.32f, c.y + s * 0.45f},
        Vector2{c.x + s * 0.48f, c.y}, col);
}

void IconPause(Vector2 c, float s, Color col) {
    const float w = s * 0.26f, h = s * 0.9f, gap = s * 0.14f;
    DrawRectangleRec(Rectangle{c.x - gap - w, c.y - h / 2, w, h}, col);
    DrawRectangleRec(Rectangle{c.x + gap, c.y - h / 2, w, h}, col);
}

void IconNext(Vector2 c, float s, Color col) {
    Tri(Vector2{c.x - s * 0.45f, c.y - s * 0.38f}, Vector2{c.x - s * 0.45f, c.y + s * 0.38f},
        Vector2{c.x + s * 0.22f, c.y}, col);
    DrawRectangleRec(Rectangle{c.x + s * 0.26f, c.y - s * 0.38f, s * 0.14f, s * 0.76f}, col);
}

void IconPrev(Vector2 c, float s, Color col) {
    Tri(Vector2{c.x + s * 0.45f, c.y - s * 0.38f}, Vector2{c.x + s * 0.45f, c.y + s * 0.38f},
        Vector2{c.x - s * 0.22f, c.y}, col);
    DrawRectangleRec(Rectangle{c.x - s * 0.40f, c.y - s * 0.38f, s * 0.14f, s * 0.76f}, col);
}

void IconShuffle(Vector2 c, float s, Color col) {
    const float x0 = c.x - s * 0.5f, x1 = c.x + s * 0.38f;
    const float yT = c.y - s * 0.3f, yB = c.y + s * 0.3f;
    const float t = std::max(1.5f, s * 0.09f);
    DrawLineEx(Vector2{x0, yT}, Vector2{x1, yB}, t, col);
    DrawLineEx(Vector2{x0, yB}, Vector2{x1, yT}, t, col);
    // Arrowheads on the right ends
    Tri(Vector2{x1 + s * 0.16f, yB}, Vector2{x1 - s * 0.06f, yB - s * 0.14f},
        Vector2{x1 - s * 0.06f, yB + s * 0.14f}, col);
    Tri(Vector2{x1 + s * 0.16f, yT}, Vector2{x1 - s * 0.06f, yT - s * 0.14f},
        Vector2{x1 - s * 0.06f, yT + s * 0.14f}, col);
}

void IconRepeat(Vector2 c, float s, Color col, bool one) {
    const float r = s * 0.42f;
    const float t = std::max(1.5f, s * 0.09f);
    DrawRing(c, r - t / 2, r + t / 2, 40.0f, 320.0f, 24, col);
    // Arrowhead at the gap
    const float ang = 40.0f * DEG2RAD;
    const Vector2 tip{c.x + r * std::cos(ang), c.y + r * std::sin(ang)};
    Tri(Vector2{tip.x + s * 0.05f, tip.y + s * 0.16f}, Vector2{tip.x - s * 0.16f, tip.y - s * 0.06f},
        Vector2{tip.x + s * 0.16f, tip.y - s * 0.10f}, col);
    if (one) {
        TextCentered("1", c, s * 0.55f, col);
    }
}

void IconVolume(Vector2 c, float s, Color col, float level) {
    const float bx = c.x - s * 0.42f;
    DrawRectangleRec(Rectangle{bx, c.y - s * 0.16f, s * 0.22f, s * 0.32f}, col);
    Tri(Vector2{bx + s * 0.08f, c.y}, Vector2{bx + s * 0.42f, c.y - s * 0.36f},
        Vector2{bx + s * 0.42f, c.y + s * 0.36f}, col);
    if (level > 0.02f) {
        DrawRing(c, s * 0.18f, s * 0.18f + std::max(1.5f, s * 0.08f), -55.0f, 55.0f, 12, col);
    }
    if (level > 0.55f) {
        DrawRing(c, s * 0.36f, s * 0.36f + std::max(1.5f, s * 0.08f), -55.0f, 55.0f, 12, col);
    }
}

void IconNote(Vector2 c, float s, Color col) {
    const float r = s * 0.18f;
    const Vector2 head{c.x - s * 0.12f, c.y + s * 0.28f};
    DrawCircleV(head, r, col);
    const float t = std::max(1.5f, s * 0.08f);
    DrawLineEx(Vector2{head.x + r - t / 2, head.y}, Vector2{head.x + r - t / 2, c.y - s * 0.36f}, t, col);
    DrawLineEx(Vector2{head.x + r - t, c.y - s * 0.36f}, Vector2{head.x + r + s * 0.26f, c.y - s * 0.24f},
               t * 1.6f, col);
}

std::string FormatTime(float seconds) {
    if (seconds < 0 || !std::isfinite(seconds)) seconds = 0;
    const int total = static_cast<int>(seconds);
    char buf[16];
    if (total >= 3600) {
        std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
    } else {
        std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
    }
    return buf;
}

}  // namespace ui
