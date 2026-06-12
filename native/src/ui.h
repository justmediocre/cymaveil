#pragma once

#include <string>

#include "raylib.h"

namespace ui {

// Theme tokens ported from src/index.css. Defaults are the dark palette; the
// light palette comes from LightTheme(). Use ApplyTheme() to switch the active
// global at runtime.
struct Theme {
    Color bg{10, 10, 11, 255};
    Color surface{19, 19, 21, 255};
    Color elevated{26, 26, 29, 255};
    Color hover{34, 34, 38, 255};
    Color border{42, 42, 46, 255};
    Color borderSubtle{30, 30, 34, 255};
    Color text{237, 237, 239, 255};
    Color textSecondary{122, 122, 130, 255};
    Color textTertiary{74, 74, 82, 255};
    Color accent{212, 165, 116, 255};
};

extern Theme theme;

// The two ported palettes. ApplyTheme swaps `theme` to one of them.
Theme DarkTheme();
Theme LightTheme();
void ApplyTheme(bool light);

void Init();      // loads fonts (system TTF with fallback to raylib default)
void Shutdown();

// ── Text ──
void Text(const std::string& s, Vector2 pos, float size, Color c);
Vector2 Measure(const std::string& s, float size);
// Draws s clipped to maxWidth with a trailing ellipsis when truncated.
void TextEllipsis(const std::string& s, Vector2 pos, float maxWidth, float size, Color c);
void TextRight(const std::string& s, Vector2 posRight, float size, Color c);
void TextCentered(const std::string& s, Vector2 center, float size, Color c);
// Like TextCentered, but when the text is wider than maxWidth it is clipped to
// a maxWidth-wide window centred on `center` and slowly scrolls back and forth
// (pausing at each end) so the whole string can be read, like the web app's
// marquee for long track titles. Stateless: the phase is driven by GetTime().
void TextMarqueeCentered(const std::string& s, Vector2 center, float maxWidth,
                         float size, Color c);
// True if a TextMarqueeCentered call actually scrolled (text overflowed) since
// the last NewFrame(). UpdatePacing reads this to keep frames flowing while a
// marquee is mid-scroll, since its phase advances off GetTime() each frame.
bool MarqueeActive();

// ── Interaction ──
// Per-frame keyboard consumption. HandleInput acts on a key first, then later
// draw functions re-read the same per-frame input state; without this they
// would re-trigger off the very key press that already opened/closed them
// (e.g. KEY_X both opens the brush editor and, the same frame, toggled its
// erase mode). Call NewFrame() once at the top of each frame to reset the set,
// ConsumeKey() right after acting on a key, and use KeyPressed() instead of
// raw IsKeyPressed() anywhere that must ignore an already-consumed press.
void NewFrame();
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
// otherwise.
int TextInput(Rectangle r, std::string* text, float size);

// ── Icons (pure geometry, sized to fit a square of `size` at center c) ──
void IconPlay(Vector2 c, float size, Color col);
void IconPause(Vector2 c, float size, Color col);
void IconNext(Vector2 c, float size, Color col);
void IconPrev(Vector2 c, float size, Color col);
void IconShuffle(Vector2 c, float size, Color col);
void IconRepeat(Vector2 c, float size, Color col, bool one);
void IconVolume(Vector2 c, float size, Color col, float level);
void IconNote(Vector2 c, float size, Color col);
void IconHeart(Vector2 c, float size, Color col, bool filled);
void IconClose(Vector2 c, float size, Color col);
void IconQueue(Vector2 c, float size, Color col);  // stacked-list glyph
void IconPlus(Vector2 c, float size, Color col);
void IconSearch(Vector2 c, float size, Color col);  // magnifier (ring + handle)
void IconBrush(Vector2 c, float size, Color col);   // paintbrush (handle + bristles)

std::string FormatTime(float seconds);

}  // namespace ui
