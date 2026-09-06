// Now Playing — port of views/NowPlayingView.tsx with NowPlaying.tsx (title
// block), ProgressBar.tsx, Controls.tsx and VolumeControl.tsx, drawn around
// the ArtView art stack.
#include "app.h"

#include <algorithm>
#include <cmath>

#include "raymath.h"
#include "rlgl.h"

#include "ui.h"

namespace {

constexpr float kPanelMaxW = 512.0f;  // max-w-lg
constexpr float kInnerMaxW = 448.0f;  // max-w-md
constexpr float kPanelH = 20 + 50 + 16 + 52 + 16 + 48 + 20;

}  // namespace

Visualizer::Style App::ResolvedStyle() {
    const Track* cur = player_.Current();
    const std::string trackId = cur != nullptr ? cur->id : std::string{};
    if (manualPick_ >= 0 && manualPickTrack_ != trackId) manualPick_ = -1;  // cleared on track change
    if (manualPick_ >= 0) return static_cast<Visualizer::Style>(manualPick_);
    if (config_.visualizerStyle == "random") {
        // Re-pick on every track change, deferred to the displayed art swap
        // for cross-album changes so it lines up with the transition.
        const std::string key = trackId + "|" + artView_.Displayed().path;
        if (!hasRandomPick_ || randomPickTrack_ != key) {
            if (!hasRandomPick_ || trackId != randomPickTrack_.substr(0, randomPickTrack_.find('|'))) {
                randomPick_ = static_cast<Visualizer::Style>(GetRandomValue(0, Visualizer::kStyleCount - 1));
            }
            randomPickTrack_ = key;
            hasRandomPick_ = true;
        }
        return randomPick_;
    }
    hasRandomPick_ = false;
    return Visualizer::StyleFromName(config_.visualizerStyle, Visualizer::Style::FullSurface);
}

void App::CycleVisualizer() {
    if (!player_.IsPlaying()) return;
    const Track* cur = player_.Current();
    const int current = static_cast<int>(ResolvedStyle());
    manualPick_ = (current + 1) % Visualizer::kStyleCount;
    manualPickTrack_ = cur != nullptr ? cur->id : std::string{};
    if (config_.visualizerStyle != "random") {
        config_.visualizerStyle = Visualizer::StyleName(static_cast<Visualizer::Style>(manualPick_));
        config_.Save();
    }
    Toast(std::string("Visualizer: ") + Visualizer::StyleLabel(static_cast<Visualizer::Style>(manualPick_)));
}

void App::UpdateForeground() {
    if (!config_.depthLayers) return;

    // Warm the mask for the incoming art as soon as the track changes, so
    // it's ready by the time the swap finishes.
    const Track* cur = player_.Current();
    const Album* curAlbum = cur != nullptr ? library_.AlbumById(cur->albumId) : nullptr;
    const Art* curArt = ResolveArt(cur, curAlbum);
    if (curArt != nullptr) {
        const std::string key = ArtKey(curArt->path);
        if (fg_.checkedKey != key) {
            fg_.checkedKey = key;
            if (!DepthEngine::MaskExists(key)) depth_.Request(key, curArt->path);
        }
    }

    // Build the texture for the art actually on screen.
    const Art& shown = artView_.Displayed();
    const std::string shownKey = ArtKey(shown.path);
    if (shown.Valid() && fg_.artKey != shownKey && DepthEngine::MaskExists(shownKey)) BuildForeground(shown);

    DepthEngine::Result result;
    while (depth_.PollResult(&result)) {
        if (result.ok && shown.Valid() && result.albumId == shownKey) BuildForeground(shown);
    }
}

void App::BuildForeground(const Art& artRef) {
    Image art = LoadImage(artRef.path.c_str());
    if (art.data == nullptr) return;
    Image mask = LoadImage(DepthEngine::MaskPath(ArtKey(artRef.path)).c_str());
    if (mask.data == nullptr) {
        UnloadImage(art);
        return;
    }
    ImageFormat(&art, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    ImageFormat(&mask, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
    ImageResize(&mask, art.width, art.height);
    auto* ap = static_cast<unsigned char*>(art.data);
    const auto* mp = static_cast<const unsigned char*>(mask.data);
    for (int i = 0; i < art.width * art.height; i++) ap[i * 4 + 3] = mp[i];
    UnloadImage(mask);

    if (fg_.tex.id != 0) UnloadTexture(fg_.tex);
    fg_.tex = LoadTextureFromImage(art);
    UnloadImage(art);
    GenTextureMipmaps(&fg_.tex);
    SetTextureFilter(fg_.tex, TEXTURE_FILTER_TRILINEAR);
    fg_.artKey = ArtKey(artRef.path);
    MarkActivity();
}

void App::UpdateContour() {
    if (view_ != View::NowPlaying || !artView_.HasDisplayed()) return;
    if (ResolvedStyle() != Visualizer::Style::ContourBars) return;
    const std::string& path = artView_.Displayed().path;
    if (contour_.artPath == path) return;
    contour_.artPath = path;
    contour_.data = ExtractContour(path);
}

void App::DrawNowPlayingView(Rectangle r, bool immersive) {
    const Track* cur = player_.Current();
    if (cur == nullptr) return;
    const Album* album = library_.AlbumById(cur->albumId);
    const Art& shown = artView_.Displayed();
    const bool hasFg = config_.depthLayers && fg_.tex.id != 0 && shown.Valid() && fg_.artKey == ArtKey(shown.path);
    const Color accent = shown.Valid() ? shown.accent : ui::theme.accent;

    // ── Layout: flex-col centred, gap-6, px-6 pb-6 (+pt-6 immersive) ──
    const float pt = immersive ? 24.0f : 0.0f;
    const float contentH = r.height - pt - 24;
    const float artSize = std::min(340.0f, 0.55f * static_cast<float>(GetScreenWidth()));
    const float artRowH = std::min(contentH - 24 - kPanelH, 0.55f * r.height);
    const float blockH = artRowH + 24 + kPanelH;
    const float top = r.y + pt + std::max(0.0f, (contentH - blockH) / 2);
    const float cx = r.x + r.width / 2;
    const Rectangle artRect{cx - artSize / 2, top + (artRowH - artSize) / 2, artSize, artSize};
    const float panelW = std::min(kPanelMaxW, r.width - 48);
    const Rectangle panel{cx - panelW / 2, top + artRowH + 24, panelW, kPanelH};

    // Immersive: the panel and back link fade with the cursor after idle.
    if (fullscreen_) {
        const float target = (GetTime() - lastActivity_ < 2.5) ? 1.0f : 0.0f;
        const float dt = std::min(GetFrameTime(), 1.0f / 30.0f);
        const float step = dt / 0.25f;
        if (ctrlFade_ < target) ctrlFade_ = std::min(target, ctrlFade_ + step);
        else if (ctrlFade_ > target) ctrlFade_ = std::max(target, ctrlFade_ - step);
    } else {
        ctrlFade_ = 1.0f;
    }
    const float cf = ctrlFade_;
    const bool ctrlOn = cf > 0.5f;

    // ── Back button (hidden in immersive): absolute top-14 left-4 ──
    if (!immersive && cf > 0.01f) {
        const Vector2 pos{r.x + 16, r.y + 56};
        const float w = 16 + 4 + ui::Measure("Back", 14).x;
        const Rectangle b{pos.x - 4, pos.y - 4, w + 8, 28};
        const bool hov = ctrlOn && ui::Hover(b);
        const float t = ui::Ease("np.back", hov ? 1.0f : 0.0f, 0.15f, 0.0f);
        const Color col = Fade(ui::Mix(ui::theme.textSecondary, ui::theme.text, t), cf);
        ui::IconChevronLeft(Vector2{pos.x + 8 - 2 * t, pos.y + 10}, 16, col);
        ui::TextV("Back", pos.x + 20 - 2 * t, pos.y + 10, 14, col);
        if (ctrlOn && ui::Clicked(b)) {
            Navigate(previousView_);
            return;
        }
    }

    // ── Album art stack ──
    const bool artHover = ui::Hover(artRect);
    const Visualizer::Style style = ResolvedStyle();
    ArtView::DrawArgs da;
    da.artRect = artRect;
    da.viewWidth = r.width;
    da.cover = shown.Valid() ? art_.Get(shown.path) : nullptr;
    da.playing = player_.IsPlaying();
    da.ambientGlow = config_.ambientGlow;
    da.lightTheme = ui::theme.light;
    da.hovered = artHover;
    da.drawOverlay = [&](Rectangle local) {
        if (config_.canvasVisualizer && player_.IsPlaying()) {
            const Visualizer::FrameStyle fs = Visualizer::ComputeFrameStyle(
                config_.visualizerIntensity / 100.0f, accent, shown.hasSecondary ? &shown.accentSecondary : nullptr,
                hasFg);
            visualizer_.Draw(style, local, fs, style == Visualizer::Style::ContourBars ? &contour_.data : nullptr);
        }
        if (hasFg) {
            const float side = static_cast<float>(std::min(fg_.tex.width, fg_.tex.height));
            const Rectangle src{(fg_.tex.width - side) / 2, (fg_.tex.height - side) / 2, side, side};
            DrawTexturePro(fg_.tex, src, local, Vector2{0, 0}, 0, WHITE);
        }
    };
    artView_.Draw(da);
    if (ui::Clicked(artRect) && !menu_.open) CycleVisualizer();

    // Top-right chips over the art: processing pill, or paint-mask button on hover.
    if (config_.depthLayers && shown.Valid()) {
        const std::string key = ArtKey(shown.path);
        const bool processing = depth_.Busy() && !DepthEngine::MaskExists(key);
        if (processing) {
            const float w = 12 + 6 + ui::Measure("Processing", 10, ui::Face::Mono).x + 16;
            const Rectangle chip{artRect.x + artRect.width - 8 - w, artRect.y + 8, w, 22};
            ui::RoundedRect(chip, 6, Fade(BLACK, 0.55f));
            ui::IconSpinner(Vector2{chip.x + 8 + 6, chip.y + 11}, 12, Color{204, 204, 204, 255});
            ui::TextV("Processing", chip.x + 8 + 12 + 6, chip.y + 11, 10, Color{204, 204, 204, 255}, ui::Face::Mono);
        } else {
            const Rectangle b{artRect.x + artRect.width - 8 - 28, artRect.y + 8, 28, 28};
            const float vis = ui::Ease("np.brushbtn", artHover || ui::Hover(b) ? 1.0f : 0.0f, 0.15f, 0.0f);
            if (vis > 0.01f) {
                ui::RoundedRect(b, 6, Fade(BLACK, 0.55f * vis));
                ui::IconBrush(Vector2{b.x + 14, b.y + 14}, 14, Fade(Color{204, 204, 204, 255}, vis));
                if (ui::Clicked(b)) {
                    OpenBrushEditor(shown);
                    return;
                }
            }
        }
    }

    if (cf <= 0.01f) return;

    // ── Controls glass panel: rounded-2xl px-6 py-5, gap-4 ──
    backdrop_.DrawGlassRounded(panel, Fade(ui::theme.glassSurface, cf), 16, config_.glassBlur);
    const float innerW = std::min(kInnerMaxW, panelW - 48);
    const float ix = cx - innerW / 2;
    float y = panel.y + 20;

    // Title block (NowPlaying.tsx): text-center px-12, actions at right-4
    {
        const float textW = innerW - 32 - 96;
        ui::TextMarqueeCentered(cur->title, Vector2{cx, y + 14}, textW, 18, Fade(ui::theme.text, cf),
                                ui::Face::DisplaySemi);
        std::vector<ui::Span> spans{{cur->artist, Fade(ui::theme.textSecondary, cf)}};
        if (album != nullptr) {
            spans.push_back({"  ·  ", Fade(ui::theme.textTertiary, cf)});
            spans.push_back({album->title, Fade(ui::theme.textTertiary, cf)});
        }
        // Clip the artist line to the same width as the title.
        ui::BeginClip(static_cast<int>(cx - textW / 2 - 6), static_cast<int>(y + 28),
                      static_cast<int>(textW + 12), 24);
        ui::TextSpansCentered(spans, Vector2{cx, y + 28 + 2 + 10}, 14);
        ui::EndClip();

        // Heart + add-to-playlist (gap-1), vertically centred in the block
        const float ay = y + 25;
        const bool fav = playlists_.IsFavorite(cur->id);
        const Rectangle heartR{ix + innerW - 16 - 24 - 4 - 24, ay - 12, 24, 24};
        if (ui::IconButton("np.fav", heartR, Fade(fav ? ui::theme.accent : ui::theme.textTertiary, cf),
                           Fade(fav ? ui::theme.accent : ui::theme.text, cf), 1.15f, 0.85f,
                           [fav](Vector2 c, Color col) { ui::IconHeart(c, 18, col, fav); }) && ctrlOn) {
            playlists_.ToggleFavorite(cur->id);
        }
        const Rectangle plusR{ix + innerW - 16 - 24, ay - 12, 24, 24};
        if (ui::IconButton("np.plus", plusR, Fade(ui::theme.textTertiary, cf), Fade(ui::theme.text, cf), 1.1f, 0.9f,
                           [](Vector2 c, Color col) { ui::IconPlus(c, 14, col); }) && ctrlOn) {
            OpenTrackMenu(cur->id, plusR, true);
        }
    }
    y += 50 + 16;

    // Progress bar (ProgressBar.tsx): px-4, 4px track, thumb on hover, times mt-2
    {
        const Rectangle trackR{ix + 16, y + 12, innerW - 32, 4};
        const Rectangle hit{trackR.x, y, trackR.width, 28};
        const float length = player_.TimeLength();
        const float played = player_.TimePlayed();
        if (!seekDragging_) seekValue_ = length > 0 ? played / length : 0;
        const bool wasDragging = seekDragging_;
        if (ctrlOn) ui::Slider(trackR, &seekValue_, &seekDragging_);
        if (wasDragging && !seekDragging_) player_.SeekTo(seekValue_ * length);
        const float p = Clamp(seekValue_, 0.0f, 1.0f);
        ui::RoundedRect(trackR, 2, Fade(ui::theme.border, cf));
        if (p > 0) ui::RoundedRect(Rectangle{trackR.x, trackR.y, trackR.width * p, 4}, 2, Fade(ui::theme.text, cf));
        const bool hov = ctrlOn && (ui::Hover(hit) || seekDragging_);
        const float ha = ui::Ease("np.prog", hov ? 1.0f : 0.0f, 0.15f, 0.0f);
        const float fx = trackR.x + trackR.width * p;
        if (ha > 0.01f) {
            // leading-edge glow: 24px accent blob blur 12 @ 0.5, thumb 12px w/ accent glow
            ui::RoundedRect(Rectangle{fx - 12, trackR.y + 2 - 12, 24, 24}, 12, Fade(ui::theme.accent, 0.5f * ha * cf), 12);
            ui::Shadow(Rectangle{fx - 6, trackR.y + 2 - 6, 12, 12}, 6, 0, 0, 8, 0, Fade(ui::theme.accent, ha * cf));
            DrawCircleV(Vector2{fx, trackR.y + 2}, 6, Fade(ui::theme.text, ha * cf));
        }
        const float shownTime = seekDragging_ ? seekValue_ * length : played;
        ui::Text(ui::FormatTime(shownTime), Vector2{trackR.x, y + 28 + 8}, 11, Fade(ui::theme.textTertiary, cf), ui::Face::Mono);
        ui::TextRight(ui::FormatTime(length), Vector2{trackR.x + trackR.width, y + 28 + 8}, 11,
                      Fade(ui::theme.textTertiary, cf), ui::Face::Mono);
    }
    y += 52 + 16;

    // Controls (Controls.tsx) + volume (VolumeControl.tsx)
    {
        const float cy = y + 24;
        const auto btn = [&](const std::string& key, float x, float size, Color col, Color hoverCol, float hs,
                             float ts, const std::function<void(Vector2, Color)>& draw) {
            const Rectangle b{x - size / 2, cy - size / 2, size, size};
            return ui::IconButton(key, b, Fade(col, cf), Fade(hoverCol, cf), hs, ts, draw) && ctrlOn;
        };
        // gap-2: shuffle(40) prev(40) play(48) next(40) repeat(40) → centres
        const float xs = cx - 24 - 8 - 40 - 8 - 20;
        const float xp = cx - 24 - 8 - 20;
        const float xn = cx + 24 + 8 + 20;
        const float xr = cx + 24 + 8 + 40 + 8 + 20;
        const bool shuffle = player_.Shuffle();
        if (btn("np.shuffle", xs, 40, shuffle ? ui::theme.accent : ui::theme.textTertiary, ui::theme.text, 1.1f, 0.9f,
                [](Vector2 c, Color col) { ui::IconShuffle(c, 16, col); })) {
            player_.ToggleShuffle();
        }
        if (shuffle) DrawCircleV(Vector2{xs, cy + 20 + 2 - 2}, 2, Fade(ui::theme.accent, cf));
        if (btn("np.prev", xp, 40, ui::theme.textSecondary, ui::theme.text, 1.1f, 0.85f,
                [](Vector2 c, Color col) { ui::IconSkipBack(c, 20, col); })) {
            player_.Prev();
            manualSkip_ = true;
        }
        {
            const Rectangle b{cx - 24, cy - 24, 48, 48};
            const bool hov = ctrlOn && ui::Hover(b);
            const float sc = ui::Spring("np.play#s", hov && IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? 0.92f : hov ? 1.08f : 1.0f, 1.0f);
            DrawCircleV(Vector2{cx, cy}, 24 * sc, Fade(ui::theme.text, cf));
            // Glyph swap: scale 0.5 -> 1, opacity 0 -> 1 over 0.2s
            const float sw = ui::Ease(player_.IsPlaying() ? "np.play#pause" : "np.play#play", 1.0f, 0.2f, 0.0f);
            const float gs = (0.5f + 0.5f * sw) * sc;
            const Color gc = Fade(ui::theme.bg, sw * cf);
            if (player_.IsPlaying()) ui::IconPause(Vector2{cx, cy}, 22 * gs, gc);
            else ui::IconPlay(Vector2{cx + 1, cy}, 22 * gs, gc);
            if (ctrlOn && ui::Clicked(b)) player_.TogglePause();
        }
        if (btn("np.next", xn, 40, ui::theme.textSecondary, ui::theme.text, 1.1f, 0.85f,
                [](Vector2 c, Color col) { ui::IconSkipForward(c, 20, col); })) {
            player_.Next();
            manualSkip_ = true;
        }
        const bool repeatOn = player_.Repeat() != RepeatMode::Off;
        const bool one = player_.Repeat() == RepeatMode::One;
        if (btn("np.repeat", xr, 40, repeatOn ? ui::theme.accent : ui::theme.textTertiary, ui::theme.text, 1.1f, 0.9f,
                [one](Vector2 c, Color col) { ui::IconRepeat(c, 16, col, one); })) {
            player_.CycleRepeat();
        }
        if (repeatOn) DrawCircleV(Vector2{xr, cy + 20}, 2, Fade(ui::theme.accent, cf));

        // Volume: absolute left-4 of the max-w-md container
        const float vol = player_.Volume();
        const float vx = ix + 16 + 20;
        const Rectangle vb{vx - 20, cy - 20, 40, 40};
        if (btn("np.vol", vx, 40, ui::theme.textTertiary, ui::theme.text, 1.0f, 0.9f,
                [vol](Vector2 c, Color col) { ui::IconVolume(c, 20, col, vol); })) {
            if (vol > 0.001f) {
                prevVolume_ = vol;
                player_.SetVolume(0);
            } else {
                player_.SetVolume(prevVolume_ > 0.01f ? prevVolume_ : 0.75f);
            }
        }
        // Popup: opens on hover, closes 400ms after leaving; wheel adjusts ±5
        const Rectangle popup{vx - 18, cy - 20 - 8 - 120, 36, 120};
        const bool overControl = ui::Hover(vb) || (volumeOpen_ && ui::Hover(popup));
        if (overControl && ctrlOn) {
            volumeOpen_ = true;
            volumeCloseAt_ = 0;
            if (GetMouseWheelMove() != 0) player_.SetVolume(vol + (GetMouseWheelMove() > 0 ? 0.05f : -0.05f));
        } else if (volumeOpen_ && !volumeDragging_) {
            if (volumeCloseAt_ == 0) volumeCloseAt_ = GetTime() + 0.4;
            if (GetTime() >= volumeCloseAt_) {
                volumeOpen_ = false;
                volumeCloseAt_ = 0;
            }
        }
        const float po = ui::Ease("np.volpop", volumeOpen_ ? 1.0f : 0.0f, 0.15f, 0.0f);
        if (po > 0.01f) {
            const Rectangle d = ui::Scaled(Rectangle{popup.x, popup.y + 4 * (1 - po), popup.width, popup.height}, 0.9f + 0.1f * po);
            backdrop_.DrawGlassRounded(d, Fade(ui::theme.glassSurface, po * cf), 12, config_.glassBlur);
            const Rectangle track{d.x + d.width / 2 - 2, d.y + 12, 4, 96};
            ui::RoundedRect(track, 2, Fade(ui::theme.border, po * cf));
            float v = vol;
            // Vertical drag over the track area
            const Rectangle hit{d.x + 8, track.y, d.width - 16, track.height};
            if (ctrlOn && ui::Clicked(hit)) volumeDragging_ = true;
            if (volumeDragging_) {
                v = Clamp(1.0f - (GetMousePosition().y - track.y) / track.height, 0.0f, 1.0f);
                player_.SetVolume(v);
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) volumeDragging_ = false;
            }
            const float fillH = track.height * v;
            ui::RoundedRect(Rectangle{track.x, track.y + track.height - fillH, 4, fillH}, 2, Fade(ui::theme.textSecondary, po * cf));
            DrawCircleV(Vector2{track.x + 2, track.y + track.height - fillH}, 6, Fade(ui::theme.text, po * cf));
        }
    }
}
