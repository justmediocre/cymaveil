#pragma once

#include <functional>
#include <string>
#include <vector>

#include "raylib.h"

namespace ui {

// Theme tokens ported from src/index.css. Defaults are the dark palette; the
// light palette comes from LightTheme(). Use ApplyTheme() to switch the active
// global at runtime.
struct Theme {
    bool light = false;
    Color bg{10, 10, 11, 255};              // --bg-primary
    Color surface{19, 19, 21, 255};         // --bg-surface
    Color elevated{26, 26, 29, 255};        // --bg-elevated
    Color hover{34, 34, 38, 255};           // --bg-hover
    Color border{42, 42, 46, 255};          // --border
    Color borderSubtle{30, 30, 34, 255};    // --border-subtle
    Color text{237, 237, 239, 255};         // --text-primary
    Color textSecondary{122, 122, 130, 255};
    Color textTertiary{74, 74, 82, 255};
    Color accent{212, 165, 116, 255};
    Color accentDim{212, 165, 116, 38};     // --accent-dim: accent @ 0.15
    Color glassBg{10, 10, 11, 158};         // --glass-bg: rgba(10,10,11,0.62)
    Color glassSurface{19, 19, 21, 148};    // --glass-bg-surface: rgba(19,19,21,0.58)
};

extern Theme theme;

// The two ported palettes. ApplyTheme swaps `theme` to one of them.
Theme DarkTheme();
Theme LightTheme();
void ApplyTheme(bool light);

// The Electron renderer's three families, embedded at build time:
//   Outfit (body, weights 400/500/600/700), Bricolage Grotesque (display
//   headings, 600/700/800) and JetBrains Mono (numerals, 400).
enum class Face {
    Sans,          // Outfit Regular      — body text
    SansMedium,    // Outfit Medium       — font-medium labels
    SansSemiBold,  // Outfit SemiBold     — font-semibold
    SansBold,      // Outfit Bold
    DisplaySemi,   // Bricolage SemiBold  — Now Playing title
    Display,       // Bricolage Bold      — page headings, section labels
    DisplayBlack,  // Bricolage ExtraBold — the brand wordmark (font-black)
    Mono,          // JetBrains Mono      — track numbers, durations, times
};

void Init();      // loads the embedded fonts (fallback to raylib default)
void Shutdown();
// Call once per frame before drawing: resets per-frame input state, advances
// the hover/transition animations, and evicts stale ones.
void NewFrame();

// ── Text ──
// `tracking` is extra letter spacing in px (CSS letter-spacing).
void Text(const std::string& s, Vector2 pos, float size, Color c, Face face = Face::Sans,
          float tracking = 0.0f);
Vector2 Measure(const std::string& s, float size, Face face = Face::Sans, float tracking = 0.0f);
// Draws s clipped to maxWidth with a trailing ellipsis when truncated.
void TextEllipsis(const std::string& s, Vector2 pos, float maxWidth, float size, Color c,
                  Face face = Face::Sans);
void TextRight(const std::string& s, Vector2 posRight, float size, Color c, Face face = Face::Sans);
void TextCentered(const std::string& s, Vector2 center, float size, Color c,
                  Face face = Face::Sans, float tracking = 0.0f);
// Vertically centred on cy, left-aligned at x.
void TextV(const std::string& s, float x, float cy, float size, Color c, Face face = Face::Sans);
// Like TextCentered, but when the text is wider than maxWidth it is clipped to
// a maxWidth-wide window centred on `center` and slowly scrolls back and forth
// (pausing at each end) so the whole string can be read, like the web app's
// marquee for long track titles. Stateless: the phase is driven by GetTime().
void TextMarqueeCentered(const std::string& s, Vector2 center, float maxWidth, float size,
                         Color c, Face face = Face::Sans);
// True if a TextMarqueeCentered call actually scrolled (text overflowed) since
// the last NewFrame(). UpdatePacing reads this to keep frames flowing.
bool MarqueeActive();
// Rich "artist · album" line: parts drawn left-to-right, centred as a whole.
struct Span {
    std::string text;
    Color color;
};
void TextSpansCentered(const std::vector<Span>& spans, Vector2 center, float size, Face face = Face::Sans);

// ── Transitions ──
// CSS-transition stand-in: returns a value that eases toward `target` over
// `seconds`, persisted across frames under `key`. Use for hover colours and
// scales so state changes glide instead of popping. Keys are arbitrary strings
// (build them from a widget name + index/id); unused keys are dropped after a
// few frames so per-row keys are fine.
float Ease(const std::string& key, float target, float seconds, float initial = -1.0f);
// Spring variant (Motion's whileHover/whileTap default): overshoots slightly.
float Spring(const std::string& key, float target, float initial = -1.0f);
// True while any Ease/Spring is still moving (pacing hint).
bool Animating();
// Mix two colours (t in 0..1) — for eased hover colour changes.
Color Mix(Color a, Color b, float t);
Color WithAlpha(Color c, float alpha);
// Easing curves used by the web app.
float EaseOutExpo(float t);   // cubic-bezier(0.22, 1, 0.36, 1) approximation
float EaseOut(float t);
float EaseInOut(float t);

// ── Interaction ──
void ConsumeKey(int key);
bool KeyPressed(int key);
// While blocked (an overlay like a context menu is open), Hover/Clicked
// return false so widgets underneath ignore the mouse. The overlay itself
// reads input through HoverRaw/ClickedRaw.
void BlockInput(bool blocked);
bool Hover(Rectangle r);
bool Clicked(Rectangle r);  // left button pressed inside r this frame
bool HoverRaw(Rectangle r);
bool ClickedRaw(Rectangle r);
// Horizontal drag slider over r; value in [0,1]. Returns true while the user
// is changing the value. `dragging` is caller-persisted state.
bool Slider(Rectangle r, float* value, bool* dragging);
// Mouse-wheel scrolling + scrollbar for a clipped area. Clamps *scroll.
void ScrollArea(Rectangle view, float contentHeight, float* scroll);
// Single-line edit box; call every frame while focused. Appends typed
// characters to *text (UTF-8), handles backspace. Returns Enter=1, Esc=-1, 0
// otherwise. Draws only the text + caret; the caller draws the field chrome.
int TextInput(Rectangle r, std::string* text, float size, Face face = Face::Sans,
              Color color = theme.text);
// Grow a rectangle about its centre (for hover/tap scale effects).
Rectangle Scaled(Rectangle r, float scale);
Rectangle Inset(Rectangle r, float dx, float dy);

// ── Shapes (anti-aliased, shader based) ──
// Rounded rectangle fill. `radius` in px; radii larger than half the size give
// a pill. `soft` > 0 feathers the edge over that many px (blurred shape).
void RoundedRect(Rectangle r, float radius, Color c, float soft = 0.0f);
void RoundedRectLines(Rectangle r, float radius, float thickness, Color c);
// Texture drawn with rounded corners. src is a sub-rect of tex (negative
// height flips, as with DrawTexturePro).
void RoundedTexture(const Texture2D& tex, Rectangle src, Rectangle dst, float radius, Color tint,
                    float soft = 0.0f);
// CSS box-shadow: offset, blur radius, spread, colour (drawn behind r).
void Shadow(Rectangle r, float radius, float offX, float offY, float blur, float spread, Color c);
// CSS inset box-shadow with no offset: `inset 0 0 blur color`.
void InnerShadow(Rectangle r, float radius, float blur, Color c);
// Cover-crop: the largest centred square of a texture (object-fit: cover).
Rectangle CoverSrc(const Texture2D& tex);
// Draws the crop of tex that covers dst (object-fit: cover for any aspect).
Rectangle CoverSrcFor(const Texture2D& tex, Rectangle dst);

// ── Icons (ported from src/components/Icons.tsx; 24-unit grid scaled to `size`) ──
void IconPlay(Vector2 c, float size, Color col);
void IconPause(Vector2 c, float size, Color col);
void IconSkipForward(Vector2 c, float size, Color col);
void IconSkipBack(Vector2 c, float size, Color col);
void IconShuffle(Vector2 c, float size, Color col);
void IconRepeat(Vector2 c, float size, Color col, bool one);
void IconVolume(Vector2 c, float size, Color col, float level);  // 0 mute, <0.5 low, else high
void IconSun(Vector2 c, float size, Color col);
void IconMoon(Vector2 c, float size, Color col);
void IconList(Vector2 c, float size, Color col);
void IconHeart(Vector2 c, float size, Color col, bool filled);
void IconSidebar(Vector2 c, float size, Color col);
void IconMusicNote(Vector2 c, float size, Color col);
void IconDisc(Vector2 c, float size, Color col, float stroke = 1.8f);
void IconSearch(Vector2 c, float size, Color col);
void IconChevronLeft(Vector2 c, float size, Color col);
void IconSettings(Vector2 c, float size, Color col);
void IconExpand(Vector2 c, float size, Color col);
void IconShrink(Vector2 c, float size, Color col);
void IconFolder(Vector2 c, float size, Color col);
void IconPlus(Vector2 c, float size, Color col, float stroke = 2.5f);
void IconClose(Vector2 c, float size, Color col, float stroke = 2.5f);
void IconCheck(Vector2 c, float size, Color col);
void IconBrush(Vector2 c, float size, Color col);
void IconSpinner(Vector2 c, float size, Color col);  // rotating dashed ring

// ── Widgets ──
// Pill button (rounded-full, px-3 py-1.5, text-xs font-medium). Optional icon
// callback draws at the given centre. Hover scales 1.05, tap 0.95 (eased).
// Returns true on click. `key` identifies the transition state.
using IconFn = std::function<void(Vector2 c, float size, Color col)>;
bool Pill(const std::string& key, Vector2 pos, const std::string& label, Color bg, Color fg,
          IconFn icon = nullptr, float iconSize = 14.0f, Rectangle* outRect = nullptr);
float PillWidth(const std::string& label, bool hasIcon);
// Round icon button (w-10 h-10 style): draws icon at the centre in `col`, with
// hover colour `hoverCol` and hover scale. Returns true on click.
bool IconButton(const std::string& key, Rectangle r, Color col, Color hoverCol, float hoverScale,
                float tapScale, const std::function<void(Vector2, Color)>& draw);
// Settings toggle (40x22 pill). Returns true when clicked (caller flips value).
bool Toggle(const std::string& key, Vector2 pos, bool on);
// Settings range slider (96px track). Returns true while changing.
bool RangeSlider(Rectangle r, float* value01, bool* dragging);

std::string FormatTime(float seconds);

}  // namespace ui
