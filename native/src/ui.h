#pragma once

#include <string>

#include "raylib.h"

namespace ui {

// Theme tokens ported from src/index.css (dark theme).
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

void Init();      // loads fonts (system TTF with fallback to raylib default)
void Shutdown();

// ── Text ──
void Text(const std::string& s, Vector2 pos, float size, Color c);
Vector2 Measure(const std::string& s, float size);
// Draws s clipped to maxWidth with a trailing ellipsis when truncated.
void TextEllipsis(const std::string& s, Vector2 pos, float maxWidth, float size, Color c);
void TextRight(const std::string& s, Vector2 posRight, float size, Color c);
void TextCentered(const std::string& s, Vector2 center, float size, Color c);

// ── Interaction ──
bool Hover(Rectangle r);
bool Clicked(Rectangle r);  // left button pressed inside r this frame
// Horizontal drag slider over r; value in [0,1]. Returns true while the user
// is changing the value. `dragging` is caller-persisted state.
bool Slider(Rectangle r, float* value, bool* dragging);
// Mouse-wheel scrolling + scrollbar for a clipped area. Clamps *scroll.
void ScrollArea(Rectangle view, float contentHeight, float* scroll);

// ── Icons (pure geometry, sized to fit a square of `size` at center c) ──
void IconPlay(Vector2 c, float size, Color col);
void IconPause(Vector2 c, float size, Color col);
void IconNext(Vector2 c, float size, Color col);
void IconPrev(Vector2 c, float size, Color col);
void IconShuffle(Vector2 c, float size, Color col);
void IconRepeat(Vector2 c, float size, Color col, bool one);
void IconVolume(Vector2 c, float size, Color col, float level);
void IconNote(Vector2 c, float size, Color col);

std::string FormatTime(float seconds);

}  // namespace ui
