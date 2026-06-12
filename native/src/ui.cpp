#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "raymath.h"

namespace ui {

Theme theme;

Theme DarkTheme() { return Theme{}; }

// [data-theme='light'] from src/index.css — warm paper neutrals with a
// muted-bronze accent.
Theme LightTheme() {
    return Theme{
        .bg = {246, 245, 241, 255},
        .surface = {255, 255, 255, 255},
        .elevated = {238, 238, 233, 255},
        .hover = {228, 228, 222, 255},
        .border = {221, 221, 214, 255},
        .borderSubtle = {232, 232, 226, 255},
        .text = {26, 26, 27, 255},
        .textSecondary = {110, 110, 114, 255},
        .textTertiary = {160, 160, 166, 255},
        .accent = {154, 107, 58, 255},
    };
}

void ApplyTheme(bool light) { theme = light ? LightTheme() : DarkTheme(); }

namespace {

// Glyphs downscaled from a single large atlas look smeared at UI sizes, so
// each requested pixel size gets its own atlas, rasterized on first use.
unsigned char* g_fontData = nullptr;
int g_fontDataSize = 0;
std::vector<int> g_cps;
std::unordered_map<int, Font> g_fonts;

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
    for (int c = 32; c <= 0x24F; c++) cps.push_back(c);   // ASCII + Latin-1 + Latin Extended-A/B
    for (int c = 0x370; c <= 0x3FF; c++) cps.push_back(c);   // Greek and Coptic (e.g. µ)
    for (int c = 0x400; c <= 0x4FF; c++) cps.push_back(c);   // Cyrillic
    for (int c = 0x2010; c <= 0x2027; c++) cps.push_back(c);  // dashes, quotes
    cps.push_back(0x2030);
    cps.push_back(0x2122);  // ™
    return cps;
}

// Framebuffer pixels per screen unit; 1.0 unless the window is DPI-scaled.
float DpiScale() {
    const int sw = GetScreenWidth();
    return sw > 0 ? static_cast<float>(GetRenderWidth()) / static_cast<float>(sw) : 1.0f;
}

Font& FontPx(int px) {
    auto it = g_fonts.find(px);
    if (it == g_fonts.end()) {
        Font f = LoadFontFromMemory(".ttf", g_fontData, g_fontDataSize, px, g_cps.data(),
                                    static_cast<int>(g_cps.size()));
        SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
        it = g_fonts.emplace(px, f).first;
    }
    return it->second;
}

struct Face {
    Font font;
    float size;
    float spacing;
};

Face FaceFor(float size) {
    if (g_fontData == nullptr) return {GetFontDefault(), size, size / 10.0f};
    const float scale = DpiScale();
    const int px = std::max(1, static_cast<int>(std::lround(size * scale)));
    return {FontPx(px), static_cast<float>(px) / scale, 0.0f};
}

// DrawTriangle culls based on winding; draw both windings so icon geometry
// never silently disappears.
void Tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    DrawTriangle(a, b, c, col);
    DrawTriangle(a, c, b, col);
}

}  // namespace

void Init() {
    g_cps = Codepoints();
    for (const char* path : kFontCandidates) {
        if (std::filesystem::exists(path)) {
            int dataSize = 0;
            unsigned char* data = LoadFileData(path, &dataSize);
            if (data != nullptr && dataSize > 0) {
                g_fontData = data;
                g_fontDataSize = dataSize;
                TraceLog(LOG_INFO, "UI: font %s, dpi scale %.2f", path, DpiScale());
                break;
            }
            if (data != nullptr) UnloadFileData(data);
        }
    }
    if (g_fontData == nullptr) TraceLog(LOG_WARNING, "UI: no system font found, using default");
}

void Shutdown() {
    for (auto& [px, font] : g_fonts) UnloadFont(font);
    g_fonts.clear();
    if (g_fontData != nullptr) {
        UnloadFileData(g_fontData);
        g_fontData = nullptr;
        g_fontDataSize = 0;
    }
}

void Text(const std::string& s, Vector2 pos, float size, Color c) {
    const Face f = FaceFor(size);
    // Snap to the device pixel grid so atlas texels map 1:1 to pixels.
    const float scale = DpiScale();
    pos.x = std::round(pos.x * scale) / scale;
    pos.y = std::round(pos.y * scale) / scale;
    DrawTextEx(f.font, s.c_str(), pos, f.size, f.spacing, c);
}

Vector2 Measure(const std::string& s, float size) {
    const Face f = FaceFor(size);
    return MeasureTextEx(f.font, s.c_str(), f.size, f.spacing);
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

void TextMarqueeCentered(const std::string& s, Vector2 center, float maxWidth,
                         float size, Color c) {
    const Vector2 m = Measure(s, size);
    if (m.x <= maxWidth) {
        Text(s, Vector2{center.x - m.x / 2, center.y - m.y / 2}, size, c);
        return;
    }

    // Ping-pong scroll: pause at the start, glide left to reveal the end, pause,
    // glide back. Timing is a pure function of GetTime() so no per-track state
    // is needed; speed is in pixels/second so long titles take proportionally
    // longer to traverse.
    const float overflow = m.x - maxWidth;
    const float speed = 36.0f;
    const float pause = 1.6f;
    const float travel = overflow / speed;
    const float period = 2.0f * (pause + travel);
    const float t = static_cast<float>(std::fmod(GetTime(), period));

    float off;
    if (t < pause)
        off = 0.0f;
    else if (t < pause + travel)
        off = (t - pause) * speed;
    else if (t < 2.0f * pause + travel)
        off = overflow;
    else
        off = overflow - (t - (2.0f * pause + travel)) * speed;
    off = std::clamp(off, 0.0f, overflow);

    const float left = center.x - maxWidth / 2;
    const float top = center.y - m.y / 2;
    const float textX = left - off;

    // Draws the (fixed-position) string clipped to the horizontal band
    // [x0, x1) in screen pixels, tinted to `a` of its alpha.
    auto band = [&](int x0, int x1, float a) {
        if (x1 <= x0) return;
        Color col = c;
        col.a = static_cast<unsigned char>(std::clamp(c.a * a, 0.0f, 255.0f));
        BeginScissorMode(x0, static_cast<int>(std::floor(top)), x1 - x0,
                         static_cast<int>(std::ceil(m.y)));
        Text(s, Vector2{textX, top}, size, col);
        EndScissorMode();
    };

    // Fade the text into the background on whichever side still hides text,
    // hinting there's more that way. The fade *depth* ramps with how much is
    // hidden — reaching a full fade once `fade` px have scrolled off — so it
    // eases in and out as the scroll nears each end rather than snapping on/off.
    // It's a handful of contiguous strips with stepped alpha: smooth enough to
    // read as a gradient without a shader or render texture.
    const float fade = std::min(size, maxWidth * 0.35f);
    auto smooth = [](float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    const float strL = smooth(off / fade);              // 0 at the left end → 1
    const float strR = smooth((overflow - off) / fade);  // 0 at the right end → 1

    const int lpx = static_cast<int>(std::floor(left));
    const int rpx = static_cast<int>(std::ceil(left + maxWidth));
    const int lEnd = static_cast<int>(std::round(left + fade));
    const int rBeg = static_cast<int>(std::round(left + maxWidth - fade));

    band(lEnd, rBeg, 1.0f);  // solid middle

    const int kSteps = 10;
    for (int i = 0; i < kSteps; i++) {
        const int x0 = lpx + (lEnd - lpx) * i / kSteps;
        const int x1 = lpx + (lEnd - lpx) * (i + 1) / kSteps;
        const float edge = (i + 0.5f) / kSteps;  // 0 at outer edge → 1 inner
        band(x0, x1, 1.0f - strL * (1.0f - edge));
    }
    for (int i = 0; i < kSteps; i++) {
        const int x0 = rBeg + (rpx - rBeg) * i / kSteps;
        const int x1 = rBeg + (rpx - rBeg) * (i + 1) / kSteps;
        const float edge = 1.0f - (i + 0.5f) / kSteps;  // 1 inner → 0 outer edge
        band(x0, x1, 1.0f - strR * (1.0f - edge));
    }
}

namespace {
bool g_inputBlocked = false;
std::unordered_set<int> g_consumedKeys;
}

void NewFrame() { g_consumedKeys.clear(); }

void ConsumeKey(int key) { g_consumedKeys.insert(key); }

bool KeyPressed(int key) {
    return g_consumedKeys.find(key) == g_consumedKeys.end() && IsKeyPressed(key);
}

void BlockInput(bool blocked) { g_inputBlocked = blocked; }

bool HoverRaw(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }

bool ClickedRaw(Rectangle r) {
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && HoverRaw(r);
}

bool Hover(Rectangle r) { return !g_inputBlocked && HoverRaw(r); }

bool Clicked(Rectangle r) { return !g_inputBlocked && ClickedRaw(r); }

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

int TextInput(Rectangle r, std::string* text, float size) {
    DrawRectangleRounded(r, 0.25f, 6, theme.elevated);
    DrawRectangleRoundedLinesEx(r, 0.25f, 6, 1, theme.accent);

    // While an overlay (e.g. a context menu) is blocking input, draw the field
    // but swallow all keyboard activity: the text must not change under the
    // overlay, and the Esc/Enter that dismisses the overlay must not be read
    // here as a field action.
    if (!g_inputBlocked) {
        int cp;
        while ((cp = GetCharPressed()) != 0) {
            char buf[5] = {};
            int n = 0;
            const char* utf8 = CodepointToUTF8(cp, &n);
            for (int i = 0; i < n; i++) buf[i] = utf8[i];
            *text += buf;
        }
        if ((KeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !text->empty()) {
            // Drop one UTF-8 codepoint from the end
            size_t i = text->size() - 1;
            while (i > 0 && (static_cast<unsigned char>((*text)[i]) & 0xC0) == 0x80) i--;
            text->erase(i);
        }
    }

    const float pad = 10;
    const Vector2 m = Measure(*text, size);
    BeginScissorMode(static_cast<int>(r.x + pad), static_cast<int>(r.y),
                     static_cast<int>(r.width - pad * 2), static_cast<int>(r.height));
    // Keep the caret in view when the text outgrows the box
    const float shift = std::max(0.0f, m.x - (r.width - pad * 2 - 4));
    const Vector2 pos{r.x + pad - shift, r.y + (r.height - m.y) / 2};
    Text(*text, pos, size, theme.text);
    if (std::fmod(GetTime(), 1.0) < 0.6) {
        DrawRectangleRec(Rectangle{pos.x + m.x + 2, r.y + 6, 1.5f, r.height - 12}, theme.text);
    }
    EndScissorMode();

    if (g_inputBlocked) return 0;
    if (KeyPressed(KEY_ENTER) || KeyPressed(KEY_KP_ENTER)) return 1;
    if (KeyPressed(KEY_ESCAPE)) return -1;
    return 0;
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

void IconHeart(Vector2 c, float s, Color col, bool filled) {
    const float r = s * 0.26f;
    const Vector2 l{c.x - r * 0.95f, c.y - s * 0.12f};
    const Vector2 rt{c.x + r * 0.95f, c.y - s * 0.12f};
    const Vector2 tip{c.x, c.y + s * 0.42f};
    if (filled) {
        DrawCircleV(l, r, col);
        DrawCircleV(rt, r, col);
        Tri(Vector2{l.x - r * 0.92f, l.y + r * 0.36f}, Vector2{rt.x + r * 0.92f, rt.y + r * 0.36f},
            tip, col);
        Tri(Vector2{l.x - r * 0.6f, l.y + r * 0.6f}, Vector2{rt.x + r * 0.6f, rt.y + r * 0.6f}, tip,
            col);
    } else {
        const float t = std::max(1.5f, s * 0.09f);
        DrawRing(l, r - t / 2, r + t / 2, 120.0f, 320.0f, 20, col);
        DrawRing(rt, r - t / 2, r + t / 2, 220.0f, 420.0f, 20, col);
        DrawLineEx(Vector2{l.x - r * 0.92f, l.y + r * 0.42f}, tip, t, col);
        DrawLineEx(Vector2{rt.x + r * 0.92f, rt.y + r * 0.42f}, tip, t, col);
    }
}

void IconClose(Vector2 c, float s, Color col) {
    const float e = s * 0.34f;
    const float t = std::max(1.5f, s * 0.1f);
    DrawLineEx(Vector2{c.x - e, c.y - e}, Vector2{c.x + e, c.y + e}, t, col);
    DrawLineEx(Vector2{c.x - e, c.y + e}, Vector2{c.x + e, c.y - e}, t, col);
}

void IconQueue(Vector2 c, float s, Color col) {
    const float t = std::max(1.5f, s * 0.1f);
    const float x0 = c.x - s * 0.45f, x1 = c.x + s * 0.45f;
    for (int i = -1; i <= 1; i++) {
        const float y = c.y + i * s * 0.3f;
        // Short bullet + line, like the web's queue glyph
        DrawCircleV(Vector2{x0 + t / 2, y}, t * 0.7f, col);
        DrawLineEx(Vector2{x0 + s * 0.22f, y}, Vector2{x1, y}, t, col);
    }
}

void IconPlus(Vector2 c, float s, Color col) {
    const float e = s * 0.42f;
    const float t = std::max(1.5f, s * 0.1f);
    DrawLineEx(Vector2{c.x - e, c.y}, Vector2{c.x + e, c.y}, t, col);
    DrawLineEx(Vector2{c.x, c.y - e}, Vector2{c.x, c.y + e}, t, col);
}

void IconSearch(Vector2 c, float s, Color col) {
    const float r = s * 0.28f;
    const float t = std::max(1.5f, s * 0.1f);
    // Lens sits up-left so the handle can trail to the bottom-right.
    const Vector2 lens{c.x - s * 0.12f, c.y - s * 0.12f};
    DrawRing(lens, r - t / 2, r + t / 2, 0.0f, 360.0f, 24, col);
    const float d = r * 0.7071f;  // 45° offset to the rim
    DrawLineEx(Vector2{lens.x + d, lens.y + d}, Vector2{c.x + s * 0.42f, c.y + s * 0.42f}, t, col);
}
void IconBrush(Vector2 c, float s, Color col) {
    const float t = std::max(1.5f, s * 0.1f);
    const Vector2 handle{c.x + s * 0.36f, c.y - s * 0.36f};  // upper-right
    const Vector2 ferrule{c.x - s * 0.02f, c.y + s * 0.02f};
    const Vector2 tip{c.x - s * 0.34f, c.y + s * 0.34f};  // lower-left bristle tip
    DrawLineEx(handle, ferrule, t, col);      // slim handle
    DrawLineEx(ferrule, tip, t * 2.0f, col);  // fatter bristles
    DrawCircleV(tip, t * 0.9f, col);          // rounded tip
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
