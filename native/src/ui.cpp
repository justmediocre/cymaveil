#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "raymath.h"
#include "rlgl.h"

#include "fonts_data.h"

namespace ui {

Theme theme;

Theme DarkTheme() { return Theme{}; }

// [data-theme='light'] from src/index.css — warm paper neutrals with a
// muted-bronze accent.
Theme LightTheme() {
    Theme t;
    t.light = true;
    t.bg = {246, 245, 241, 255};
    t.surface = {255, 255, 255, 255};
    t.elevated = {238, 238, 233, 255};
    t.hover = {228, 228, 222, 255};
    t.border = {221, 221, 214, 255};
    t.borderSubtle = {232, 232, 226, 255};
    t.text = {26, 26, 27, 255};
    t.textSecondary = {110, 110, 114, 255};
    t.textTertiary = {160, 160, 166, 255};
    t.accent = {154, 107, 58, 255};
    t.accentDim = {154, 107, 58, 26};       // 0.10
    t.glassBg = {246, 245, 241, 153};       // 0.60
    t.glassSurface = {255, 255, 255, 140};  // 0.55
    return t;
}

void ApplyTheme(bool light) { theme = light ? LightTheme() : DarkTheme(); }

namespace {

// ── Fonts ──
// Glyphs downscaled from a single large atlas look smeared at UI sizes, so
// each requested (face, pixel size) pair gets its own atlas, rasterized on
// first use.
struct FaceData {
    const unsigned char* data = nullptr;
    int size = 0;
};
FaceData g_faces[8];
std::vector<int> g_cps;
std::unordered_map<int, Font> g_fonts;  // key: face << 16 | px
bool g_fontsOk = false;

std::vector<int> Codepoints() {
    std::vector<int> cps;
    for (int c = 32; c <= 0x24F; c++) cps.push_back(c);   // ASCII + Latin-1 + Latin Extended-A/B
    for (int c = 0x370; c <= 0x3FF; c++) cps.push_back(c);   // Greek and Coptic (e.g. µ)
    for (int c = 0x400; c <= 0x4FF; c++) cps.push_back(c);   // Cyrillic
    for (int c = 0x2010; c <= 0x2027; c++) cps.push_back(c);  // dashes, quotes, bullets
    for (int c = 0x2190; c <= 0x2193; c++) cps.push_back(c);  // arrows (shortcut hints)
    cps.push_back(0x2030);
    cps.push_back(0x2122);  // ™
    cps.push_back(0x00B7);  // ·
    return cps;
}

// Framebuffer pixels per screen unit; 1.0 unless the window is DPI-scaled.
float DpiScale() {
    const int sw = GetScreenWidth();
    return sw > 0 ? static_cast<float>(GetRenderWidth()) / static_cast<float>(sw) : 1.0f;
}

Font& FontPx(Face face, int px) {
    const int key = (static_cast<int>(face) << 16) | px;
    auto it = g_fonts.find(key);
    if (it == g_fonts.end()) {
        const FaceData& fd = g_faces[static_cast<int>(face)];
        Font f = LoadFontFromMemory(".ttf", fd.data, fd.size, px, g_cps.data(),
                                    static_cast<int>(g_cps.size()));
        SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
        it = g_fonts.emplace(key, f).first;
    }
    return it->second;
}

struct FaceRef {
    Font font;
    float size;
    float spacing;
};

FaceRef FaceFor(Face face, float size, float tracking) {
    if (!g_fontsOk) return {GetFontDefault(), size, size / 10.0f + tracking};
    const float scale = DpiScale();
    const int px = std::max(1, static_cast<int>(std::lround(size * scale)));
    return {FontPx(face, px), static_cast<float>(px) / scale, tracking};
}

// DrawTriangle culls based on winding; draw both windings so icon geometry
// never silently disappears.
void Tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    DrawTriangle(a, b, c, col);
    DrawTriangle(a, c, b, col);
}

// ── Rounded-shape shader ──
// One fragment shader handles every anti-aliased rounded shape: a signed
// distance to a rounded box (in px) turns into coverage, optionally feathered
// (`soft`, for box-shadows) or inverted into an inset shadow. Textures pass
// through so album art can be clipped to CSS border-radius exactly.
const char* kRoundFs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 size;
uniform float radius;
uniform float soft;
uniform float inset;
uniform vec4 srcRect;
out vec4 finalColor;
float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}
void main() {
    vec2 uv = (fragTexCoord - srcRect.xy) / (srcRect.zw - srcRect.xy);
    vec2 p = (uv - 0.5) * size;
    float r = min(radius, min(size.x, size.y) * 0.5);
    float d = sdRoundBox(p, size * 0.5, r);
    float a;
    if (inset > 0.5) {
        // Inside only, strongest at the edge, fading inward over `soft` px.
        a = (d > 0.0) ? 0.0 : (1.0 - smoothstep(-soft, 0.0, d));
        a = 1.0 - a;
        a = a * a;                    // gaussian-ish falloff
        a = (d > 0.0) ? 0.0 : a;
    } else if (soft > 0.0) {
        a = 1.0 - smoothstep(-soft, soft, d);
    } else {
        a = 1.0 - smoothstep(-0.75, 0.75, d);
    }
    vec4 tex = texture(texture0, fragTexCoord);
    finalColor = tex * colDiffuse * fragColor * vec4(1.0, 1.0, 1.0, a);
}
)";

Shader g_round{};
int g_locSize = -1, g_locRadius = -1, g_locSoft = -1, g_locInset = -1, g_locSrc = -1;
Texture2D g_white{};

void EnsureRound() {
    if (g_round.id == 0) {
        g_round = LoadShaderFromMemory(nullptr, kRoundFs);
        g_locSize = GetShaderLocation(g_round, "size");
        g_locRadius = GetShaderLocation(g_round, "radius");
        g_locSoft = GetShaderLocation(g_round, "soft");
        g_locInset = GetShaderLocation(g_round, "inset");
        g_locSrc = GetShaderLocation(g_round, "srcRect");
    }
    if (g_white.id == 0) {
        Image img = GenImageColor(2, 2, WHITE);
        g_white = LoadTextureFromImage(img);
        UnloadImage(img);
    }
}

void DrawRound(const Texture2D& tex, Rectangle src, Rectangle dst, float radius, float soft,
               bool inset, Color tint) {
    if (dst.width <= 0 || dst.height <= 0 || tint.a == 0) return;
    EnsureRound();
    // Flush anything batched under the previous uniform values before we
    // change them: SetShaderValue applies immediately, the batch draws later.
    rlDrawRenderBatchActive();
    BeginShaderMode(g_round);
    const Vector2 size{dst.width, dst.height};
    const float insetF = inset ? 1.0f : 0.0f;
    // A feathered (soft) outer edge extends beyond dst: draw a quad padded by
    // `soft` and stretch the uv range so the shader still evaluates the box
    // as dst. (Texture samples outside src clamp to its edge texels.)
    const float pad = (!inset && soft > 0.0f) ? soft : 0.0f;
    const float ex = pad / dst.width, ey = pad / dst.height;
    const Rectangle quad{dst.x - pad, dst.y - pad, dst.width + 2 * pad, dst.height + 2 * pad};
    // raylib samples [src.x, src.x+|w|] x [src.y, src.y+|h|] whatever the sign
    // (negative sizes only flip the vertex order), so normalise with the
    // absolute extents and keep the sign on the padded rect.
    const float aw = std::fabs(src.width), ah = std::fabs(src.height);
    const Rectangle qsrc{src.x - aw * ex, src.y - ah * ey, std::copysign(aw * (1 + 2 * ex), src.width),
                         std::copysign(ah * (1 + 2 * ey), src.height)};
    const float u0 = src.x / tex.width, v0 = src.y / tex.height;
    const float u1 = (src.x + aw) / tex.width, v1 = (src.y + ah) / tex.height;
    const float srcRect[4] = {u0, v0, u1, v1};
    SetShaderValue(g_round, g_locSize, &size, SHADER_UNIFORM_VEC2);
    SetShaderValue(g_round, g_locRadius, &radius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(g_round, g_locSoft, &soft, SHADER_UNIFORM_FLOAT);
    SetShaderValue(g_round, g_locInset, &insetF, SHADER_UNIFORM_FLOAT);
    SetShaderValue(g_round, g_locSrc, srcRect, SHADER_UNIFORM_VEC4);
    DrawTexturePro(tex, qsrc, quad, Vector2{0, 0}, 0, tint);
    rlDrawRenderBatchActive();
    EndShaderMode();
}

// ── Transitions ──
struct AnimState {
    float value = 0;
    float velocity = 0;
    int lastFrame = 0;
    bool spring = false;
};
std::unordered_map<std::string, AnimState> g_anims;
int g_frame = 0;
float g_dt = 1.0f / 60.0f;
bool g_animating = false;

// ── Input ──
bool g_inputBlocked = false;
std::unordered_set<int> g_consumedKeys;
bool g_marqueeActive = false;

}  // namespace

void Init() {
    g_cps = Codepoints();
    const FaceData faces[8] = {
        {kFont_Outfit_Regular, kFont_Outfit_Regular_Size},
        {kFont_Outfit_Medium, kFont_Outfit_Medium_Size},
        {kFont_Outfit_SemiBold, kFont_Outfit_SemiBold_Size},
        {kFont_Outfit_Bold, kFont_Outfit_Bold_Size},
        {kFont_BricolageGrotesque_SemiBold, kFont_BricolageGrotesque_SemiBold_Size},
        {kFont_BricolageGrotesque_Bold, kFont_BricolageGrotesque_Bold_Size},
        {kFont_BricolageGrotesque_ExtraBold, kFont_BricolageGrotesque_ExtraBold_Size},
        {kFont_JetBrainsMono_Regular, kFont_JetBrainsMono_Regular_Size},
    };
    for (int i = 0; i < 8; i++) g_faces[i] = faces[i];
    g_fontsOk = true;
    TraceLog(LOG_INFO, "UI: embedded fonts loaded, dpi scale %.2f", DpiScale());
    EnsureRound();
}

void Shutdown() {
    for (auto& [key, font] : g_fonts) UnloadFont(font);
    g_fonts.clear();
    if (g_round.id != 0) UnloadShader(g_round);
    g_round = Shader{};
    if (g_white.id != 0) UnloadTexture(g_white);
    g_white = Texture2D{};
}

void NewFrame() {
    g_consumedKeys.clear();
    g_marqueeActive = false;
    g_frame++;
    // Clamp so a wake from event-waiting doesn't snap every transition.
    g_dt = std::min(GetFrameTime(), 1.0f / 30.0f);
    g_animating = false;
    for (auto it = g_anims.begin(); it != g_anims.end();) {
        if (g_frame - it->second.lastFrame > 120) it = g_anims.erase(it);
        else ++it;
    }
}

// ── Text ──

void Text(const std::string& s, Vector2 pos, float size, Color c, Face face, float tracking) {
    const FaceRef f = FaceFor(face, size, tracking);
    // Snap to the device pixel grid so atlas texels map 1:1 to pixels.
    const float scale = DpiScale();
    pos.x = std::round(pos.x * scale) / scale;
    pos.y = std::round(pos.y * scale) / scale;
    DrawTextEx(f.font, s.c_str(), pos, f.size, f.spacing, c);
}

Vector2 Measure(const std::string& s, float size, Face face, float tracking) {
    const FaceRef f = FaceFor(face, size, tracking);
    return MeasureTextEx(f.font, s.c_str(), f.size, f.spacing);
}

void TextEllipsis(const std::string& s, Vector2 pos, float maxWidth, float size, Color c,
                  Face face) {
    if (Measure(s, size, face).x <= maxWidth) {
        Text(s, pos, size, c, face);
        return;
    }
    std::string cut = s;
    while (!cut.empty()) {
        // Drop one UTF-8 codepoint from the end
        size_t i = cut.size() - 1;
        while (i > 0 && (static_cast<unsigned char>(cut[i]) & 0xC0) == 0x80) i--;
        cut.erase(i);
        if (Measure(cut + "…", size, face).x <= maxWidth) break;
    }
    Text(cut + "…", pos, size, c, face);
}

void TextRight(const std::string& s, Vector2 posRight, float size, Color c, Face face) {
    const float w = Measure(s, size, face).x;
    Text(s, Vector2{posRight.x - w, posRight.y}, size, c, face);
}

void TextCentered(const std::string& s, Vector2 center, float size, Color c, Face face,
                  float tracking) {
    const Vector2 m = Measure(s, size, face, tracking);
    // Trailing tracking after the last glyph shouldn't offset centring.
    const float w = m.x - (tracking > 0 ? tracking : 0);
    Text(s, Vector2{center.x - w / 2, center.y - m.y / 2}, size, c, face, tracking);
}

void TextV(const std::string& s, float x, float cy, float size, Color c, Face face) {
    const Vector2 m = Measure(s, size, face);
    Text(s, Vector2{x, cy - m.y / 2}, size, c, face);
}

void TextSpansCentered(const std::vector<Span>& spans, Vector2 center, float size, Face face) {
    float total = 0, h = 0;
    for (const auto& sp : spans) {
        const Vector2 m = Measure(sp.text, size, face);
        total += m.x;
        h = std::max(h, m.y);
    }
    float x = center.x - total / 2;
    for (const auto& sp : spans) {
        Text(sp.text, Vector2{x, center.y - h / 2}, size, sp.color, face);
        x += Measure(sp.text, size, face).x;
    }
}

void TextMarqueeCentered(const std::string& s, Vector2 center, float maxWidth, float size,
                         Color c, Face face) {
    const Vector2 m = Measure(s, size, face);
    if (m.x <= maxWidth) {
        Text(s, Vector2{center.x - m.x / 2, center.y - m.y / 2}, size, c, face);
        return;
    }

    // The marquee's phase is a function of GetTime(), so frames must keep
    // flowing for it to advance — even during its end-of-travel pauses.
    g_marqueeActive = true;

    // MarqueeText.tsx: glide 0 -> -overflow over 2 + overflow/60 s (easeInOut),
    // pause 2 s at each end (1.5 s initial delay folded into the pause).
    const float overflow = m.x - maxWidth;
    const float travel = 2.0f + overflow / 60.0f;
    const float pause = 2.0f;
    const float period = 2.0f * (pause + travel);
    const float t = static_cast<float>(std::fmod(GetTime(), period));

    float off;
    if (t < pause) off = 0.0f;
    else if (t < pause + travel) off = EaseInOut((t - pause) / travel) * overflow;
    else if (t < 2.0f * pause + travel) off = overflow;
    else off = (1.0f - EaseInOut((t - (2.0f * pause + travel)) / travel)) * overflow;
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
        BeginScissorMode(x0, static_cast<int>(std::floor(top)) - 2, x1 - x0,
                         static_cast<int>(std::ceil(m.y)) + 4);
        Text(s, Vector2{textX, top}, size, col, face);
        EndScissorMode();
    };

    // 12px edge fades (FADE_WIDTH), only on the side that still hides text.
    const float fade = 12.0f;
    const float strL = std::clamp(off / fade, 0.0f, 1.0f);
    const float strR = std::clamp((overflow - off) / fade, 0.0f, 1.0f);

    const int lpx = static_cast<int>(std::floor(left));
    const int rpx = static_cast<int>(std::ceil(left + maxWidth));
    const int lEnd = static_cast<int>(std::round(left + fade));
    const int rBeg = static_cast<int>(std::round(left + maxWidth - fade));

    band(lEnd, rBeg, 1.0f);  // solid middle

    const int kSteps = 8;
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

bool MarqueeActive() { return g_marqueeActive; }

// ── Transitions ──

float Ease(const std::string& key, float target, float seconds, float initial) {
    auto it = g_anims.find(key);
    if (it == g_anims.end()) {
        AnimState st;
        st.value = initial >= 0 ? initial : target;
        it = g_anims.emplace(key, st).first;
    }
    AnimState& st = it->second;
    st.lastFrame = g_frame;
    if (st.value != target) {
        // Exponential approach: reaches ~95% of the way in `seconds`.
        const float k = seconds > 0 ? 1.0f - std::exp(-3.0f * g_dt / seconds) : 1.0f;
        st.value += (target - st.value) * k;
        if (std::fabs(target - st.value) < 0.002f) st.value = target;
        else g_animating = true;
    }
    return st.value;
}

float Spring(const std::string& key, float target, float initial) {
    auto it = g_anims.find(key);
    if (it == g_anims.end()) {
        AnimState st;
        st.value = initial >= 0 ? initial : target;
        st.spring = true;
        it = g_anims.emplace(key, st).first;
    }
    AnimState& st = it->second;
    st.lastFrame = g_frame;
    if (st.value != target || std::fabs(st.velocity) > 0.0001f) {
        // Motion's default spring (stiffness 700, damping 35, mass 1).
        const float stiffness = 700.0f, damping = 35.0f;
        const float accel = -stiffness * (st.value - target) - damping * st.velocity;
        st.velocity += accel * g_dt;
        st.value += st.velocity * g_dt;
        if (std::fabs(target - st.value) < 0.001f && std::fabs(st.velocity) < 0.01f) {
            st.value = target;
            st.velocity = 0;
        } else {
            g_animating = true;
        }
    }
    return st.value;
}

bool Animating() { return g_animating; }

Color Mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color{static_cast<unsigned char>(a.r + (b.r - a.r) * t),
                 static_cast<unsigned char>(a.g + (b.g - a.g) * t),
                 static_cast<unsigned char>(a.b + (b.b - a.b) * t),
                 static_cast<unsigned char>(a.a + (b.a - a.a) * t)};
}

Color WithAlpha(Color c, float alpha) {
    c.a = static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    return c;
}

float EaseOutExpo(float t) {
    const float inv = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - inv * inv * inv * inv;
}

float EaseOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t);
}

float EaseInOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ── Interaction ──

void ConsumeKey(int key) { g_consumedKeys.insert(key); }

bool KeyPressed(int key) {
    return g_consumedKeys.find(key) == g_consumedKeys.end() && IsKeyPressed(key);
}

void BlockInput(bool blocked) { g_inputBlocked = blocked; }

bool HoverRaw(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }

bool ClickedRaw(Rectangle r) { return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && HoverRaw(r); }

bool Hover(Rectangle r) { return !g_inputBlocked && HoverRaw(r); }

bool Clicked(Rectangle r) { return !g_inputBlocked && ClickedRaw(r); }

bool Slider(Rectangle r, float* value, bool* dragging) {
    // Generous vertical hit area for slim bars
    Rectangle hit{r.x, r.y - 8, r.width, r.height + 16};
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
        // 6px webkit scrollbar, 3px radius, --scrollbar-thumb (border colour)
        const float thumbH = std::max(32.0f, view.height * view.height / contentHeight);
        const float thumbY = view.y + (*scroll / maxScroll) * (view.height - thumbH);
        const Rectangle thumb{view.x + view.width - 6, thumbY, 6, thumbH};
        const bool hov = Hover(Rectangle{thumb.x - 4, view.y, 14, view.height});
        RoundedRect(thumb, 3, hov ? theme.textTertiary : theme.border);
    }
}

int TextInput(Rectangle r, std::string* text, float size, Face face, Color color) {
    // While an overlay (e.g. a context menu) is blocking input, draw the field
    // but swallow all keyboard activity.
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
            size_t i = text->size() - 1;
            while (i > 0 && (static_cast<unsigned char>((*text)[i]) & 0xC0) == 0x80) i--;
            text->erase(i);
        }
    }

    const Vector2 m = Measure(*text, size, face);
    BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width),
                     static_cast<int>(r.height));
    // Keep the caret in view when the text outgrows the box
    const float shift = std::max(0.0f, m.x - (r.width - 4));
    const Vector2 pos{r.x - shift, r.y + (r.height - m.y) / 2};
    Text(*text, pos, size, color, face);
    if (std::fmod(GetTime(), 1.0) < 0.6) {
        DrawRectangleRec(Rectangle{pos.x + m.x + 1, r.y + (r.height - size) / 2 - 1, 1.5f, size + 2},
                         color);
    }
    EndScissorMode();

    if (g_inputBlocked) return 0;
    if (KeyPressed(KEY_ENTER) || KeyPressed(KEY_KP_ENTER)) return 1;
    if (KeyPressed(KEY_ESCAPE)) return -1;
    return 0;
}

Rectangle Scaled(Rectangle r, float scale) {
    const float w = r.width * scale, h = r.height * scale;
    return Rectangle{r.x + (r.width - w) / 2, r.y + (r.height - h) / 2, w, h};
}

Rectangle Inset(Rectangle r, float dx, float dy) {
    return Rectangle{r.x + dx, r.y + dy, r.width - 2 * dx, r.height - 2 * dy};
}

// ── Shapes ──

void RoundedRect(Rectangle r, float radius, Color c, float soft) {
    EnsureRound();
    DrawRound(g_white, Rectangle{0, 0, 2, 2}, r, radius, soft, false, c);
}

void RoundedRectLines(Rectangle r, float radius, float thickness, Color c) {
    const float rr = std::min(radius, std::min(r.width, r.height) / 2);
    DrawRectangleRoundedLinesEx(r, rr / std::min(r.width, r.height) * 2, 12, thickness, c);
}

void RoundedTexture(const Texture2D& tex, Rectangle src, Rectangle dst, float radius, Color tint,
                    float soft) {
    DrawRound(tex, src, dst, radius, soft, false, tint);
}

void Shadow(Rectangle r, float radius, float offX, float offY, float blur, float spread, Color c) {
    // CSS: the shadow shape is the box grown by `spread`, offset, then
    // gaussian-blurred with sigma = blur/2 — feathered over about ±blur.
    const Rectangle grown{r.x + offX - spread, r.y + offY - spread, r.width + 2 * spread,
                          r.height + 2 * spread};
    if (grown.width <= 0 || grown.height <= 0 || c.a == 0) return;
    EnsureRound();
    DrawRound(g_white, Rectangle{0, 0, 2, 2}, grown, std::max(0.0f, radius + spread), blur, false, c);
}

void InnerShadow(Rectangle r, float radius, float blur, Color c) {
    EnsureRound();
    DrawRound(g_white, Rectangle{0, 0, 2, 2}, r, radius, blur, true, c);
}

Rectangle CoverSrc(const Texture2D& tex) {
    const float side = static_cast<float>(std::min(tex.width, tex.height));
    return Rectangle{(tex.width - side) / 2.0f, (tex.height - side) / 2.0f, side, side};
}

Rectangle CoverSrcFor(const Texture2D& tex, Rectangle dst) {
    if (dst.width <= 0 || dst.height <= 0) return CoverSrc(tex);
    const float aspect = dst.width / dst.height;
    float w = static_cast<float>(tex.width), h = static_cast<float>(tex.height);
    if (w / h > aspect) w = h * aspect;
    else h = w / aspect;
    return Rectangle{(tex.width - w) / 2.0f, (tex.height - h) / 2.0f, w, h};
}

// ── Icons ──
// Each icon is drawn on the SVG's 24-unit grid. `Pen` maps grid units to
// pixels around the icon centre and provides stroked primitives with round
// caps/joins (stroke-linecap/linejoin: round in Icons.tsx).
namespace {

struct Pen {
    Vector2 c;
    float s;      // px per grid unit
    float t;      // stroke thickness px
    Color col;

    Vector2 P(float x, float y) const { return Vector2{c.x + (x - 12.0f) * s, c.y + (y - 12.0f) * s}; }
    void Dot(float x, float y, float r) const { DrawCircleV(P(x, y), r * s, col); }
    void Cap(Vector2 p) const { DrawCircleV(p, t / 2, col); }
    void Line(float x0, float y0, float x1, float y1) const {
        const Vector2 a = P(x0, y0), b = P(x1, y1);
        DrawLineEx(a, b, t, col);
        Cap(a);
        Cap(b);
    }
    void Poly(std::initializer_list<float> pts) const {
        const float* p = pts.begin();
        const size_t n = pts.size() / 2;
        for (size_t i = 0; i + 1 < n; i++) Line(p[i * 2], p[i * 2 + 1], p[i * 2 + 2], p[i * 2 + 3]);
    }
    // Stroked arc: centre (cx,cy), radius r, angles in degrees (clockwise on
    // screen, 0 = +x), with round caps.
    void Arc(float cx, float cy, float r, float a0, float a1) const {
        const Vector2 ctr = P(cx, cy);
        const float rp = r * s;
        DrawRing(ctr, rp - t / 2, rp + t / 2, a0, a1, 32, col);
        Cap(Vector2{ctr.x + rp * std::cos(a0 * DEG2RAD), ctr.y + rp * std::sin(a0 * DEG2RAD)});
        Cap(Vector2{ctr.x + rp * std::cos(a1 * DEG2RAD), ctr.y + rp * std::sin(a1 * DEG2RAD)});
    }
    void Circle(float cx, float cy, float r) const {
        const Vector2 ctr = P(cx, cy);
        DrawRing(ctr, r * s - t / 2, r * s + t / 2, 0, 360, 48, col);
    }
    void FillCircle(float cx, float cy, float r) const { DrawCircleV(P(cx, cy), r * s, col); }
    void FillTri(float x0, float y0, float x1, float y1, float x2, float y2) const {
        Tri(P(x0, y0), P(x1, y1), P(x2, y2), col);
    }
    void FillRoundRect(float x, float y, float w, float h, float rx) const {
        const Vector2 a = P(x, y);
        RoundedRect(Rectangle{a.x, a.y, w * s, h * s}, rx * s, col);
    }
    // Stroked rounded rectangle.
    void RoundRect(float x, float y, float w, float h, float rx) const {
        const Vector2 a = P(x, y);
        RoundedRectLines(Rectangle{a.x, a.y, w * s, h * s}, rx * s, t, col);
    }
};

Pen MakePen(Vector2 c, float size, Color col, float stroke = 1.8f) {
    const float s = size / 24.0f;
    return Pen{c, s, std::max(1.1f, stroke * s), col};
}

}  // namespace

void IconPlay(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col, 1.0f);
    // Filled triangle with a 1-unit round-joined stroke: soften the corners.
    p.FillTri(6.5f, 4.5f, 6.5f, 19.5f, 19.5f, 12.0f);
    p.Line(6.5f, 4.5f, 6.5f, 19.5f);
    p.Line(6.5f, 19.5f, 19.5f, 12.0f);
    p.Line(19.5f, 12.0f, 6.5f, 4.5f);
}

void IconPause(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    p.FillRoundRect(5.5f, 4, 4, 16, 1.5f);
    p.FillRoundRect(14.5f, 4, 4, 16, 1.5f);
}

void IconSkipForward(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    p.FillTri(5, 5, 5, 19, 16, 12);
    p.FillRoundRect(17, 5, 2.5f, 14, 1);
}

void IconSkipBack(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    p.FillTri(19, 5, 19, 19, 8, 12);
    p.FillRoundRect(4.5f, 5, 2.5f, 14, 1);
}

void IconShuffle(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    p.Poly({16, 3, 21, 3, 21, 8});
    p.Line(4, 20, 21, 3);
    p.Poly({21, 16, 21, 21, 16, 21});
    p.Line(15, 15, 21, 21);
    p.Line(4, 4, 9, 9);
}

void IconRepeat(Vector2 c, float size, Color col, bool one) {
    const Pen p = MakePen(c, size, col);
    p.Poly({17, 1, 21, 5, 17, 9});
    p.Line(3, 11, 3, 9);
    p.Arc(7, 9, 4, 180, 270);
    p.Line(7, 5, 21, 5);
    p.Poly({7, 23, 3, 19, 7, 15});
    p.Line(21, 13, 21, 15);
    p.Arc(17, 15, 4, 0, 90);
    p.Line(17, 19, 3, 19);
    if (one) TextCentered("1", Vector2{c.x, c.y + 0.5f * p.s}, 9.0f * p.s * 1.15f, col, Face::SansBold);
}

void IconVolume(Vector2 c, float size, Color col, float level) {
    const Pen p = MakePen(c, size, col);
    // Filled speaker polygon: 11,5 6,9 2,9 2,15 6,15 11,19
    DrawRectangleRec(Rectangle{p.P(2, 9).x, p.P(2, 9).y, 4 * p.s, 6 * p.s}, col);
    p.FillTri(6, 9, 11, 5, 11, 19);
    p.FillTri(6, 9, 11, 19, 6, 15);
    if (level > 0.001f) p.Arc(12, 12, 5, -45, 45);
    if (level >= 0.5f) p.Arc(12, 12, 10, -45, 45);
}

void IconSun(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    p.Circle(12, 12, 5);
    p.Line(12, 1, 12, 3);
    p.Line(12, 21, 12, 23);
    p.Line(4.22f, 4.22f, 5.64f, 5.64f);
    p.Line(18.36f, 18.36f, 19.78f, 19.78f);
    p.Line(1, 12, 3, 12);
    p.Line(21, 12, 23, 12);
    p.Line(4.22f, 19.78f, 5.64f, 18.36f);
    p.Line(18.36f, 5.64f, 19.78f, 4.22f);
}

void IconMoon(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    // Outer limb: r9 about (12,12) from (21,12.79) clockwise round to (11.21,3).
    p.Arc(12, 12, 9, 5.0f, 264.0f);
    // Inner limb: r7 about (16.8,7.2), the concave edge facing the centre.
    p.Arc(16.8f, 7.2f, 7, 53.0f, 217.0f);
}

void IconList(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    for (float y : {6.0f, 12.0f, 18.0f}) {
        p.Line(8, y, 21, y);
        p.Cap(p.P(3, y));
    }
}

void IconHeart(Vector2 c, float size, Color col, bool filled) {
    const Pen p = MakePen(c, size, col);
    // Two r5.5 lobes centred (7.05,8.5) and (16.95,8.5), meeting at the dip
    // (12,5.67); straight sides from the 45° points down to the tip (12,21.23).
    if (filled) {
        p.FillCircle(7.05f, 8.5f, 5.5f);
        p.FillCircle(16.95f, 8.5f, 5.5f);
        Tri(p.P(3.16f, 12.39f), p.P(20.84f, 12.39f), p.P(12, 21.23f), col);
        Tri(p.P(7.05f, 8.5f), p.P(16.95f, 8.5f), p.P(20.84f, 12.39f), col);
        Tri(p.P(7.05f, 8.5f), p.P(20.84f, 12.39f), p.P(3.16f, 12.39f), col);
        p.Line(3.16f, 12.39f, 12, 21.23f);
        p.Line(20.84f, 12.39f, 12, 21.23f);
    } else {
        p.Arc(7.05f, 8.5f, 5.5f, 135, 315);
        p.Arc(16.95f, 8.5f, 5.5f, -135, 45);
        p.Line(3.16f, 12.39f, 12, 21.23f);
        p.Line(20.84f, 12.39f, 12, 21.23f);
        p.Poly({10.94f, 4.61f, 12, 5.67f, 13.06f, 4.61f});
    }
}

void IconSidebar(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    p.RoundRect(3, 3, 18, 18, 2);
    p.Line(9, 3, 9, 21);
}

void IconMusicNote(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    p.Poly({9, 18, 9, 5, 21, 3, 21, 16});
    p.FillCircle(6, 18, 3);
    p.FillCircle(18, 16, 3);
}

void IconDisc(Vector2 c, float size, Color col, float stroke) {
    const Pen p = MakePen(c, size, col, stroke);
    p.Circle(12, 12, 10);
    p.Circle(12, 12, 3);
}

void IconSearch(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    p.Circle(11, 11, 8);
    p.Line(21, 21, 16.65f, 16.65f);
}

void IconChevronLeft(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    p.Poly({15, 18, 9, 12, 15, 6});
}

void IconSettings(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    p.Circle(12, 12, 3);
    // Feather's gear: an 8-lobed ring. Approximate with an r7 ring and eight
    // rounded teeth reaching r9.4, at 45° steps starting straight up.
    p.Circle(12, 12, 6.9f);
    for (int i = 0; i < 8; i++) {
        const float a = (i * 45.0f - 90.0f) * DEG2RAD;
        const float x0 = 12 + std::cos(a) * 6.2f, y0 = 12 + std::sin(a) * 6.2f;
        const float x1 = 12 + std::cos(a) * 9.4f, y1 = 12 + std::sin(a) * 9.4f;
        const Vector2 a0 = p.P(x0, y0), a1 = p.P(x1, y1);
        DrawLineEx(a0, a1, p.t * 2.2f, col);
        DrawCircleV(a1, p.t * 1.1f, col);
    }
}

void IconExpand(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    p.Poly({15, 3, 21, 3, 21, 9});
    p.Poly({9, 21, 3, 21, 3, 15});
    p.Line(21, 3, 14, 10);
    p.Line(3, 21, 10, 14);
}

void IconShrink(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    p.Poly({4, 14, 10, 14, 10, 20});
    p.Poly({20, 10, 14, 10, 14, 4});
    p.Line(14, 10, 21, 3);
    p.Line(3, 21, 10, 14);
}

void IconFolder(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col);
    p.Poly({2, 19, 2, 3, 9, 3, 11, 6, 22, 6, 22, 19, 2, 19});
}

void IconPlus(Vector2 c, float size, Color col, float stroke) {
    const Pen p = MakePen(c, size, col, stroke);
    p.Line(12, 5, 12, 19);
    p.Line(5, 12, 19, 12);
}

void IconClose(Vector2 c, float size, Color col, float stroke) {
    const Pen p = MakePen(c, size, col, stroke);
    p.Line(18, 6, 6, 18);
    p.Line(6, 6, 18, 18);
}

void IconCheck(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col, 2.5f);
    p.Poly({20, 6, 9, 17, 4, 12});
}

void IconBrush(Vector2 c, float size, Color col) {
    const Pen p = MakePen(c, size, col, 2.0f);
    // AlbumArt.tsx "paint mask": a pen over an open square.
    p.Poly({9, 2, 4, 2, 2, 4, 2, 20, 4, 22, 20, 22, 22, 20, 22, 15});
    p.Poly({11, 10, 18.37f, 2.63f, 21.37f, 5.63f, 14, 13, 10, 14, 11, 10});
    p.Poly({14, 13, 12.5f, 18.5f, 8, 22});
}

void IconSpinner(Vector2 c, float size, Color col) {
    const float r = size * 0.39f;
    const float t = std::max(1.2f, size * 0.11f);
    const float a0 = static_cast<float>(std::fmod(GetTime() * 360.0, 360.0));
    DrawRing(c, r - t / 2, r + t / 2, a0, a0 + 210.0f, 24, col);
}

// ── Widgets ──

float PillWidth(const std::string& label, bool hasIcon) {
    return 24.0f + Measure(label, 12, Face::SansMedium).x + (hasIcon ? 14.0f + 6.0f : 0.0f);
}

bool Pill(const std::string& key, Vector2 pos, const std::string& label, Color bg, Color fg,
          IconFn icon, float iconSize, Rectangle* outRect) {
    const bool hasIcon = static_cast<bool>(icon);
    const Rectangle r{pos.x, pos.y, PillWidth(label, hasIcon), 26.0f};
    if (outRect != nullptr) *outRect = r;
    const bool hov = Hover(r);
    const bool down = hov && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const float sc = Spring(key + "#s", down ? 0.95f : hov ? 1.05f : 1.0f, 1.0f);
    const Rectangle d = Scaled(r, sc);
    RoundedRect(d, 999, bg);
    float x = d.x + 12 * sc;
    if (hasIcon) {
        icon(Vector2{x + iconSize * sc / 2, d.y + d.height / 2}, iconSize * sc, fg);
        x += (iconSize + 6) * sc;
    }
    TextV(label, x, d.y + d.height / 2, 12 * sc, fg, Face::SansMedium);
    return Clicked(r);
}

bool IconButton(const std::string& key, Rectangle r, Color col, Color hoverCol, float hoverScale,
                float tapScale, const std::function<void(Vector2, Color)>& draw) {
    const bool hov = Hover(r);
    const bool down = hov && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const float sc = Spring(key + "#s", down ? tapScale : hov ? hoverScale : 1.0f, 1.0f);
    const float ct = Ease(key + "#c", hov ? 1.0f : 0.0f, 0.15f, 0.0f);
    const Color c = Mix(col, hoverCol, ct);
    const Vector2 centre{r.x + r.width / 2, r.y + r.height / 2};
    rlPushMatrix();
    rlTranslatef(centre.x, centre.y, 0);
    rlScalef(sc, sc, 1);
    rlTranslatef(-centre.x, -centre.y, 0);
    draw(centre, c);
    rlPopMatrix();
    return Clicked(r);
}

bool Toggle(const std::string& key, Vector2 pos, bool on) {
    const Rectangle r{pos.x, pos.y, 40, 22};
    const float t = Ease(key, on ? 1.0f : 0.0f, 0.2f);
    RoundedRect(r, 11, Mix(theme.elevated, theme.accent, t));
    RoundedRectLines(r, 11, 1, Mix(theme.border, theme.accent, t));
    const float kx = r.x + 2 + 18 * t;
    RoundedRect(Rectangle{kx, r.y + 3, 16, 16}, 8, Mix(theme.textTertiary, WHITE, t));
    return Clicked(r);
}

bool RangeSlider(Rectangle r, float* value01, bool* dragging) {
    // input[type=range]: 4px track (--border), 16px thumb (--text-primary)
    // that fades in on hover.
    const Rectangle track{r.x, r.y + r.height / 2 - 2, r.width, 4};
    const bool changed = Slider(track, value01, dragging);
    RoundedRect(track, 2, theme.border);
    const float fx = track.x + track.width * std::clamp(*value01, 0.0f, 1.0f);
    RoundedRect(Rectangle{track.x, track.y, fx - track.x, 4}, 2, theme.accent);
    const bool hov = Hover(Rectangle{r.x - 8, r.y, r.width + 16, r.height}) || *dragging;
    const float a = Ease("range#" + std::to_string(static_cast<long long>(r.x * 7919 + r.y)),
                         hov ? 1.0f : 0.0f, 0.15f, 0.0f);
    if (a > 0.01f) DrawCircleV(Vector2{fx, track.y + 2}, 8, WithAlpha(theme.text, a));
    return changed;
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
