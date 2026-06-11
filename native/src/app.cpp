#include "app.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "raymath.h"

#include "ui.h"

namespace {

constexpr float kSidebarW = 220.0f;
constexpr float kPlayerH = 88.0f;
constexpr float kMiniPlayerH = 72.0f;
constexpr float kRowH = 44.0f;

Color Brighten(Color c, float t) {
    return Color{static_cast<unsigned char>(c.r + (255 - c.r) * t),
                 static_cast<unsigned char>(c.g + (255 - c.g) * t),
                 static_cast<unsigned char>(c.b + (255 - c.b) * t), 255};
}

// Slider with track + fill + knob visuals. Returns true while interacting.
bool BarSlider(Rectangle r, float* value, bool* dragging, Color fill) {
    DrawRectangleRounded(r, 1.0f, 4, Fade(ui::theme.text, 0.12f));
    const bool interacting = ui::Slider(r, value, dragging);
    const float fillW = r.width * Clamp(*value, 0.0f, 1.0f);
    if (fillW > 1) {
        DrawRectangleRounded(Rectangle{r.x, r.y, fillW, r.height}, 1.0f, 4, fill);
    }
    if (ui::Hover(Rectangle{r.x, r.y - 6, r.width, r.height + 12}) || *dragging) {
        DrawCircleV(Vector2{r.x + fillW, r.y + r.height / 2}, 6, ui::theme.text);
    }
    return interacting;
}

}  // namespace

// ── ArtCache ──

const Texture2D* ArtCache::Get(const Album& album) {
    if (album.artPath.empty()) return nullptr;
    auto it = textures_.find(album.id);
    if (it != textures_.end()) return &it->second;
    if (!failed_.contains(album.id)) wanted_.emplace(album.id, album.artPath);
    return nullptr;
}

void ArtCache::ProcessQueue(int budget) {
    while (budget-- > 0 && !wanted_.empty()) {
        auto it = wanted_.begin();
        Image img = LoadImage(it->second.c_str());
        if (img.data != nullptr) {
            if (img.width > 600 || img.height > 600) {
                const float scale = 600.0f / std::max(img.width, img.height);
                ImageResize(&img, static_cast<int>(img.width * scale),
                            static_cast<int>(img.height * scale));
            }
            Texture2D tex = LoadTextureFromImage(img);
            UnloadImage(img);
            GenTextureMipmaps(&tex);
            SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
            textures_[it->first] = tex;
        } else {
            failed_.insert(it->first);
        }
        wanted_.erase(it);
    }
}

void ArtCache::Clear() {
    for (auto& [id, tex] : textures_) UnloadTexture(tex);
    textures_.clear();
    wanted_.clear();
    failed_.clear();
}

// ── App ──

int App::Run() {
    // Fixed-size in screenshot mode so tiling WMs float the window at the
    // requested resolution instead of fitting it into the layout.
    SetConfigFlags(screenshotPath_.empty() ? (FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT)
                                           : FLAG_MSAA_4X_HINT);
    InitWindow(1280, 800, "Cymaveil");
    SetWindowMinSize(980, 640);
    SetExitKey(KEY_NULL);  // ESC navigates, doesn't quit
    SetTargetFPS(targetFps_);
    InitAudioDevice();
    ui::Init();

    config_.Load();
    library_.Load();
    mosaic_.Rebuild(library_.Albums(), MosaicCfg());
    player_.SetVolume(config_.volume);
    player_.SetShuffle(config_.shuffle);
    player_.SetRepeat(static_cast<RepeatMode>(config_.repeat));
    visualizer_.Attach();

    for (const auto& f : startupFolders_) {
        if (DirectoryExists(f.c_str())) library_.AddFolder(f);
    }
    if (startView_ == "library") view_ = View::Library;
    else if (startView_ == "albums") view_ = View::Albums;
    else if (startView_ == "now") view_ = View::NowPlaying;
    MarkActivity();

    while (!WindowShouldClose()) Frame();

    config_.volume = player_.Volume();
    config_.shuffle = player_.Shuffle();
    config_.repeat = static_cast<int>(player_.Repeat());
    config_.Save();

    player_.Shutdown();
    visualizer_.Detach();
    mosaic_.Unload();
    vinyl_.Unload();
    if (fg_.tex.id != 0) UnloadTexture(fg_.tex);
    art_.Clear();
    ui::Shutdown();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}

void App::Frame() {
    HandleDroppedFolders();
    HandleInput();
    player_.Update();
    if (library_.PollScan()) {
        mosaic_.Rebuild(library_.Albums(), MosaicCfg());
        MarkActivity();
    }
    if (autoplay_ && !player_.HasTrack() && !library_.Tracks().empty()) {
        autoplay_ = false;
        std::vector<const Track*> all;
        for (const auto& t : library_.Tracks()) all.push_back(&t);
        PlayFromTrackList(all, 0);
        if (startView_.empty()) view_ = View::NowPlaying;
    }
    visualizer_.Update(GetFrameTime(), player_.IsPlaying());
    mosaic_.Update(GetFrameTime(), player_.IsPlaying(), MosaicCfg());
    art_.ProcessQueue(2);
    {
        const Track* cur = player_.Current();
        vinyl_.Update(GetFrameTime(), cur != nullptr ? cur->albumId : std::string{},
                      player_.IsPlaying(), manualSkip_, config_.vinylDisc);
        manualSkip_ = false;
    }
    UpdateForeground();

    const float W = static_cast<float>(GetScreenWidth());
    const float H = static_cast<float>(GetScreenHeight());
    // Full transport on Now Playing; compact mini player while browsing
    // (none at all when nothing is loaded), like the web app's AppLayout.
    const bool fullBar = view_ == View::NowPlaying;
    const float barH = fullBar ? kPlayerH : (player_.Current() != nullptr ? kMiniPlayerH : 0.0f);
    const Rectangle sidebar{0, 0, kSidebarW, H - barH};
    const Rectangle content{kSidebarW, 0, W - kSidebarW, H - barH};
    const Rectangle bar{0, H - barH, W, barH};

    BeginDrawing();
    ClearBackground(ui::theme.bg);
    mosaic_.Draw(Rectangle{0, 0, W, H}, art_, library_, MosaicCfg(), ui::theme.bg);

    switch (view_) {
        case View::Library: DrawLibraryView(content); break;
        case View::Albums: DrawAlbumsView(content); break;
        case View::AlbumDetail: DrawAlbumDetailView(content); break;
        case View::NowPlaying: DrawNowPlayingView(content); break;
    }
    DrawSidebar(sidebar);
    if (fullBar) {
        DrawPlayerBar(bar);
    } else if (barH > 0) {
        DrawMiniPlayer(bar);
    }
    if (showDebug_) DrawDebugOverlay();

    EndDrawing();

    if (!screenshotPath_.empty()) showDebug_ = true;
    // Catch a mosaic tile mid-transition in the capture
    if (!screenshotPath_.empty() && frameCount_ == 90) mosaic_.Trigger(MosaicCfg());
    if (!screenshotPath_.empty() && ++frameCount_ == 120) {
        TraceLog(LOG_INFO, "SHOT: screen %dx%d render %dx%d", GetScreenWidth(),
                 GetScreenHeight(), GetRenderWidth(), GetRenderHeight());
        // Not TakeScreenshot(): it forces the path relative to the working dir
        Image shot = LoadImageFromScreen();
        ExportImage(shot, screenshotPath_.c_str());
        UnloadImage(shot);
    }
    UpdatePacing();
}

void App::HandleInput() {
    const Vector2 d = GetMouseDelta();
    if (d.x != 0 || d.y != 0 || GetMouseWheelMove() != 0 || IsMouseButtonDown(MOUSE_BUTTON_LEFT) ||
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        MarkActivity();
    }
    while (GetKeyPressed() != 0) MarkActivity();  // drain queue: any key wakes the UI

    if (IsKeyPressed(KEY_SPACE)) player_.TogglePause();
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (IsKeyPressed(KEY_RIGHT)) {
        if (ctrl) {
            player_.Next();
            manualSkip_ = true;
        } else {
            player_.SeekTo(player_.TimePlayed() + 5.0f);
        }
    }
    if (IsKeyPressed(KEY_LEFT)) {
        if (ctrl) {
            player_.Prev();
            manualSkip_ = true;
        } else {
            player_.SeekTo(player_.TimePlayed() - 5.0f);
        }
    }
    if (IsKeyPressed(KEY_UP)) player_.SetVolume(player_.Volume() + 0.05f);
    if (IsKeyPressed(KEY_DOWN)) player_.SetVolume(player_.Volume() - 0.05f);
    if (IsKeyPressed(KEY_S)) player_.ToggleShuffle();
    if (IsKeyPressed(KEY_R)) player_.CycleRepeat();
    if (IsKeyPressed(KEY_ONE)) view_ = View::Library;
    if (IsKeyPressed(KEY_TWO)) view_ = View::Albums;
    if (IsKeyPressed(KEY_THREE)) view_ = View::NowPlaying;
    if (IsKeyPressed(KEY_ESCAPE) && view_ == View::AlbumDetail) view_ = View::Albums;
    if (IsKeyPressed(KEY_F3)) showDebug_ = !showDebug_;
    if (IsKeyPressed(KEY_B)) mosaic_.Trigger(MosaicCfg());  // manually animate a tile
}

MosaicSettings App::MosaicCfg() const {
    return MosaicSettings{config_.mosaicEnabled, config_.mosaicOpacity, config_.mosaicDensity,
                          config_.mosaicTransition, config_.mosaicFlat};
}

void App::UpdateForeground() {
    if (!config_.depthLayers) return;

    // Warm the mask for the incoming album as soon as the track changes, so
    // it's ready by the time the vinyl swap finishes.
    const Track* cur = player_.Current();
    const Album* curAlbum = cur != nullptr ? library_.AlbumById(cur->albumId) : nullptr;
    if (curAlbum != nullptr && !curAlbum->artPath.empty() && fg_.checkedAlbum != curAlbum->id) {
        fg_.checkedAlbum = curAlbum->id;
        if (!DepthEngine::MaskExists(curAlbum->id)) {
            depth_.Request(curAlbum->id, curAlbum->artPath);
        }
    }

    // Build the texture for the album actually on screen — the old album's
    // foreground persists through the vinyl retract, like the web's
    // snapshotted segmentation.
    const Album* shown = library_.AlbumById(vinyl_.DisplayedAlbumId());
    if (shown != nullptr && !shown->artPath.empty() && fg_.albumId != shown->id &&
        DepthEngine::MaskExists(shown->id)) {
        BuildForeground(*shown);
    }

    DepthEngine::Result result;
    while (depth_.PollResult(&result)) {
        if (result.ok && shown != nullptr && result.albumId == shown->id) {
            BuildForeground(*shown);
        }
    }
}

void App::BuildForeground(const Album& album) {
    Image art = LoadImage(album.artPath.c_str());
    if (art.data == nullptr) return;
    Image mask = LoadImage(DepthEngine::MaskPath(album.id).c_str());
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
    fg_.albumId = album.id;
    MarkActivity();
}

void App::HandleDroppedFolders() {
    if (!IsFileDropped()) return;
    FilePathList files = LoadDroppedFiles();
    for (unsigned int i = 0; i < files.count; i++) {
        const char* p = files.paths[i];
        if (DirectoryExists(p)) {
            library_.AddFolder(p);
        } else {
            // A dropped file adds its containing folder
            const std::string parent = std::filesystem::path(p).parent_path().string();
            if (!parent.empty() && DirectoryExists(parent.c_str())) library_.AddFolder(parent);
        }
    }
    UnloadDroppedFiles(files);
    MarkActivity();
}

void App::UpdatePacing() {
    // The whole point of the rewrite: only burn CPU/GPU when something moves.
    //  - playing + focused: 60 fps for the visualizer
    //  - playing + unfocused: 24 fps (stream still needs feeding)
    //  - recent input / scan / pending art decodes: 60 fps
    //  - otherwise: block on OS events (near-zero usage until input arrives)
    const bool busy = library_.ScanActive() || art_.HasPendingWork() || seekDragging_ ||
                      volumeDragging_ || mosaic_.Animating() || depth_.Busy() ||
                      vinyl_.Animating();
    const bool recentInput = GetTime() - lastActivity_ < 2.5;

    int fps;
    bool wait = false;
    if (player_.IsPlaying()) {
        fps = IsWindowFocused() ? 60 : 24;
    } else if (busy || recentInput) {
        fps = 60;
    } else {
        fps = 30;
        wait = true;
    }
    if (wait != eventWaiting_) {
        if (wait) EnableEventWaiting();
        else DisableEventWaiting();
        eventWaiting_ = wait;
    }
    if (fps != targetFps_) {
        SetTargetFPS(fps);
        targetFps_ = fps;
    }
}

void App::DrawSidebar(Rectangle r) {
    DrawRectangleRec(r, ui::theme.surface);
    DrawLineEx(Vector2{r.width, 0}, Vector2{r.width, r.height}, 1, ui::theme.borderSubtle);

    ui::Text("Cymaveil", Vector2{20, 22}, 26, ui::theme.accent);

    struct NavItem {
        const char* label;
        View view;
    };
    const NavItem items[] = {
        {"Library", View::Library}, {"Albums", View::Albums}, {"Now Playing", View::NowPlaying}};
    float y = 78;
    for (const auto& item : items) {
        const Rectangle row{10, y, r.width - 20, 38};
        const bool active = view_ == item.view ||
                            (item.view == View::Albums && view_ == View::AlbumDetail);
        if (active) {
            DrawRectangleRounded(row, 0.3f, 6, ui::theme.elevated);
        } else if (ui::Hover(row)) {
            DrawRectangleRounded(row, 0.3f, 6, Fade(ui::theme.hover, 0.6f));
        }
        ui::Text(item.label, Vector2{row.x + 14, row.y + 10}, 16,
                 active ? ui::theme.text : ui::theme.textSecondary);
        if (ui::Clicked(row)) view_ = item.view;
        y += 44;
    }

    // Status footer
    float fy = r.height - 78;
    if (library_.ScanActive()) {
        const ScanStatus st = library_.Status();
        const std::string label = st.total > 0
            ? TextFormat("Scanning %d / %d", st.current, st.total)
            : "Scanning...";
        ui::Text(label, Vector2{20, fy}, 13, ui::theme.textSecondary);
        const Rectangle track{20, fy + 22, r.width - 40, 4};
        DrawRectangleRounded(track, 1.0f, 4, ui::theme.border);
        if (st.total > 0) {
            const float frac = static_cast<float>(st.current) / static_cast<float>(st.total);
            DrawRectangleRounded(Rectangle{track.x, track.y, track.width * frac, track.height},
                                 1.0f, 4, ui::theme.accent);
        }
    } else {
        ui::Text(TextFormat("%d tracks  ·  %d albums", static_cast<int>(library_.Tracks().size()),
                            static_cast<int>(library_.Albums().size())),
                 Vector2{20, fy}, 13, ui::theme.textSecondary);
        ui::Text("Drop folders to add music", Vector2{20, fy + 22}, 12, ui::theme.textTertiary);
    }
}

void App::DrawPlayerBar(Rectangle r) {
    DrawRectangleRec(r, ui::theme.surface);
    DrawLineEx(Vector2{r.x, r.y}, Vector2{r.x + r.width, r.y}, 1, ui::theme.borderSubtle);

    const Track* cur = player_.Current();
    const Album* album = cur ? library_.AlbumById(cur->albumId) : nullptr;
    const float cx = r.width / 2;

    // Left: current track info
    DrawAlbumArt(Rectangle{16, r.y + 16, 56, 56}, album, 0.5f);
    if (cur != nullptr) {
        const float infoW = cx - 300 - 84;
        ui::TextEllipsis(cur->title, Vector2{84, r.y + 22}, infoW, 16, ui::theme.text);
        ui::TextEllipsis(cur->artist, Vector2{84, r.y + 44}, infoW, 13, ui::theme.textSecondary);
    }

    // Center: transport controls
    const float by = r.y + 32;
    const auto iconButton = [&](float x, float halfSize) {
        return Rectangle{cx + x - halfSize, by - halfSize, halfSize * 2, halfSize * 2};
    };

    const Rectangle shuffleR = iconButton(-110, 14);
    const Color shuffleCol = player_.Shuffle() ? ui::theme.accent
                             : ui::Hover(shuffleR) ? ui::theme.text
                                                   : ui::theme.textSecondary;
    ui::IconShuffle(Vector2{cx - 110, by}, 16, shuffleCol);
    if (ui::Clicked(shuffleR)) player_.ToggleShuffle();

    const Rectangle prevR = iconButton(-60, 14);
    ui::IconPrev(Vector2{cx - 60, by}, 18,
                 ui::Hover(prevR) ? ui::theme.text : ui::theme.textSecondary);
    if (ui::Clicked(prevR)) {
        player_.Prev();
        manualSkip_ = true;
    }

    const Rectangle playR = iconButton(0, 21);
    const bool playHover = ui::Hover(playR);
    DrawCircleV(Vector2{cx, by}, 21, playHover ? Brighten(ui::theme.accent, 0.15f) : ui::theme.accent);
    if (player_.IsPlaying()) {
        ui::IconPause(Vector2{cx, by}, 16, ui::theme.bg);
    } else {
        ui::IconPlay(Vector2{cx + 1, by}, 17, ui::theme.bg);
    }
    if (ui::Clicked(playR)) player_.TogglePause();

    const Rectangle nextR = iconButton(60, 14);
    ui::IconNext(Vector2{cx + 60, by}, 18,
                 ui::Hover(nextR) ? ui::theme.text : ui::theme.textSecondary);
    if (ui::Clicked(nextR)) {
        player_.Next();
        manualSkip_ = true;
    }

    const Rectangle repeatR = iconButton(110, 14);
    const bool repeatOn = player_.Repeat() != RepeatMode::Off;
    const Color repeatCol = repeatOn ? ui::theme.accent
                            : ui::Hover(repeatR) ? ui::theme.text
                                                 : ui::theme.textSecondary;
    ui::IconRepeat(Vector2{cx + 110, by}, 15, repeatCol, player_.Repeat() == RepeatMode::One);
    if (ui::Clicked(repeatR)) player_.CycleRepeat();

    // Seek bar with time labels
    const float length = player_.TimeLength();
    const float played = player_.TimePlayed();
    if (!seekDragging_) seekValue_ = length > 0 ? played / length : 0;
    const float barHalf = std::min(280.0f, r.width / 2 - 320);
    const Rectangle seekR{cx - barHalf, r.y + 64, barHalf * 2, 4};
    const bool wasDragging = seekDragging_;
    BarSlider(seekR, &seekValue_, &seekDragging_, ui::theme.accent);
    if (wasDragging && !seekDragging_) player_.SeekTo(seekValue_ * length);
    const float shownTime = seekDragging_ ? seekValue_ * length : played;
    ui::TextRight(ui::FormatTime(shownTime), Vector2{seekR.x - 10, r.y + 58}, 12,
                  ui::theme.textSecondary);
    ui::Text(ui::FormatTime(length), Vector2{seekR.x + seekR.width + 10, r.y + 58}, 12,
             ui::theme.textSecondary);

    // Right: volume
    float vol = player_.Volume();
    const Vector2 volIcon{r.width - 158, by};
    ui::IconVolume(volIcon, 17, ui::theme.textSecondary, vol);
    const Rectangle volR{r.width - 134, by - 2, 100, 4};
    if (BarSlider(volR, &vol, &volumeDragging_, ui::theme.text)) {
        player_.SetVolume(vol);
    }
    const Rectangle volIconR{volIcon.x - 12, volIcon.y - 12, 24, 24};
    if (ui::Clicked(volIconR)) player_.SetVolume(vol > 0.01f ? 0.0f : 0.8f);
}

void App::DrawMiniPlayer(Rectangle r) {
    const Track* cur = player_.Current();
    if (cur == nullptr) return;
    const Album* album = library_.AlbumById(cur->albumId);

    DrawRectangleRec(r, ui::theme.surface);
    DrawLineEx(Vector2{r.x, r.y}, Vector2{r.x + r.width, r.y}, 1, ui::theme.borderSubtle);

    const float cy = r.y + r.height / 2;
    const Rectangle playR{r.x + r.width - 104, cy - 20, 40, 40};
    const Rectangle nextR{r.x + r.width - 56, cy - 20, 40, 40};
    const Rectangle progressHit{r.x, r.y - 8, r.width, 20};

    // Scrubbable hairline progress sitting on the top edge (2px, 4px hovered)
    const float length = player_.TimeLength();
    if (!seekDragging_) seekValue_ = length > 0 ? player_.TimePlayed() / length : 0;
    const bool wasDragging = seekDragging_;
    ui::Slider(Rectangle{r.x, r.y - 2, r.width, 4}, &seekValue_, &seekDragging_);
    if (wasDragging && !seekDragging_) player_.SeekTo(seekValue_ * length);
    const float lineH = (ui::Hover(progressHit) || seekDragging_) ? 4.0f : 2.0f;
    DrawRectangleRec(Rectangle{r.x, r.y, r.width, lineH}, ui::theme.borderSubtle);
    DrawRectangleRec(Rectangle{r.x, r.y, r.width * Clamp(seekValue_, 0.0f, 1.0f), lineH},
                     ui::theme.accent);

    // Art thumb + track info
    DrawAlbumArt(Rectangle{r.x + 16, cy - 24, 48, 48}, album, 0.5f);
    const float infoW = playR.x - (r.x + 80) - 12;
    ui::TextEllipsis(cur->title, Vector2{r.x + 80, cy - 18}, infoW, 15, ui::theme.text);
    ui::TextEllipsis(cur->artist, Vector2{r.x + 80, cy + 2}, infoW, 12, ui::theme.textTertiary);

    // Play/pause + next
    if (ui::Hover(playR)) DrawCircleV(Vector2{playR.x + 20, cy}, 20, ui::theme.hover);
    if (player_.IsPlaying()) {
        ui::IconPause(Vector2{playR.x + 20, cy}, 15, ui::theme.text);
    } else {
        ui::IconPlay(Vector2{playR.x + 21, cy}, 16, ui::theme.text);
    }
    if (ui::Clicked(playR)) player_.TogglePause();

    if (ui::Hover(nextR)) DrawCircleV(Vector2{nextR.x + 20, cy}, 20, ui::theme.hover);
    ui::IconNext(Vector2{nextR.x + 20, cy}, 15, ui::theme.textSecondary);
    if (ui::Clicked(nextR)) {
        player_.Next();
        manualSkip_ = true;
    }

    // Anywhere else on the bar expands into Now Playing
    if (ui::Clicked(r) && !ui::Hover(playR) && !ui::Hover(nextR) && !ui::Hover(progressHit)) {
        view_ = View::NowPlaying;
    }
}

int App::DrawTrackTable(Rectangle r, const std::vector<const Track*>& tracks, float* scroll,
                        bool showAlbum) {
    const float pad = 24;
    const float numW = 44, durW = 64;
    const float flexW = r.width - pad * 2 - numW - durW;
    const float titleW = flexW * (showAlbum ? 0.42f : 0.72f);
    const float artistW = flexW * 0.28f;
    const float albumW = showAlbum ? flexW * 0.30f : 0;

    // Header
    const float hx = r.x + pad;
    ui::Text("#", Vector2{hx, r.y + 10}, 12, ui::theme.textTertiary);
    ui::Text("TITLE", Vector2{hx + numW, r.y + 10}, 12, ui::theme.textTertiary);
    ui::Text("ARTIST", Vector2{hx + numW + titleW, r.y + 10}, 12, ui::theme.textTertiary);
    if (showAlbum) {
        ui::Text("ALBUM", Vector2{hx + numW + titleW + artistW, r.y + 10}, 12,
                 ui::theme.textTertiary);
    }
    ui::TextRight("TIME", Vector2{r.x + r.width - pad, r.y + 10}, 12, ui::theme.textTertiary);
    DrawLineEx(Vector2{r.x + pad, r.y + 32}, Vector2{r.x + r.width - pad, r.y + 32}, 1,
               ui::theme.borderSubtle);

    const Rectangle list{r.x, r.y + 36, r.width, r.height - 36};
    ui::ScrollArea(list, static_cast<float>(tracks.size()) * kRowH, scroll);

    int clicked = -1;
    const Track* current = player_.Current();
    const int n = static_cast<int>(tracks.size());
    const int first = std::max(0, static_cast<int>(*scroll / kRowH));
    const int last = std::min(n, static_cast<int>((*scroll + list.height) / kRowH) + 1);

    BeginScissorMode(static_cast<int>(list.x), static_cast<int>(list.y),
                     static_cast<int>(list.width), static_cast<int>(list.height));
    for (int i = first; i < last; i++) {
        const Track& t = *tracks[i];
        const float y = list.y + i * kRowH - *scroll;
        const Rectangle row{r.x + 12, y, r.width - 24, kRowH};
        const bool isCurrent = current != nullptr && current->id == t.id;
        if (ui::Hover(row) && ui::Hover(list)) {
            DrawRectangleRounded(row, 0.2f, 6, Fade(ui::theme.hover, 0.6f));
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) clicked = i;
        }
        const float ty = y + 13;
        const Color titleCol = isCurrent ? ui::theme.accent : ui::theme.text;
        if (isCurrent) {
            ui::IconNote(Vector2{hx + 8, y + kRowH / 2}, 16, ui::theme.accent);
        } else {
            ui::Text(t.trackNum > 0 ? TextFormat("%d", t.trackNum) : "-", Vector2{hx, ty}, 15,
                     ui::theme.textTertiary);
        }
        ui::TextEllipsis(t.title, Vector2{hx + numW, ty}, titleW - 16, 15, titleCol);
        ui::TextEllipsis(t.artist, Vector2{hx + numW + titleW, ty}, artistW - 16, 14,
                         ui::theme.textSecondary);
        if (showAlbum) {
            const Album* a = library_.AlbumById(t.albumId);
            ui::TextEllipsis(a != nullptr ? a->title : "", Vector2{hx + numW + titleW + artistW, ty},
                             albumW - 16, 14, ui::theme.textSecondary);
        }
        ui::TextRight(ui::FormatTime(t.duration), Vector2{r.x + r.width - pad, ty}, 14,
                      ui::theme.textSecondary);
    }
    EndScissorMode();
    return clicked;
}

void App::PlayFromTrackList(const std::vector<const Track*>& list, int index) {
    std::vector<std::string> ids;
    ids.reserve(list.size());
    for (const Track* t : list) ids.push_back(t->id);
    player_.PlayQueue(std::move(ids), index);
    manualSkip_ = true;
    MarkActivity();
}

void App::DrawLibraryView(Rectangle r) {
    if (library_.Tracks().empty()) {
        DrawEmptyState(r);
        return;
    }
    ui::Text("Library", Vector2{r.x + 24, r.y + 24}, 28, ui::theme.text);

    std::vector<const Track*> tracks;
    tracks.reserve(library_.Tracks().size());
    for (const auto& t : library_.Tracks()) tracks.push_back(&t);

    const Rectangle table{r.x, r.y + 72, r.width, r.height - 72};
    const int clicked = DrawTrackTable(table, tracks, &libScroll_, true);
    if (clicked >= 0) PlayFromTrackList(tracks, clicked);
}

void App::DrawAlbumsView(Rectangle r) {
    if (library_.Albums().empty()) {
        DrawEmptyState(r);
        return;
    }
    ui::Text("Albums", Vector2{r.x + 24, r.y + 24}, 28, ui::theme.text);

    const float pad = 24;
    const float cardW = 172, artH = 172, cardH = artH + 50, gap = 20;
    const Rectangle grid{r.x, r.y + 72, r.width, r.height - 72};
    const int cols = std::max(1, static_cast<int>((grid.width - pad * 2 + gap) / (cardW + gap)));
    const auto& albums = library_.Albums();
    const int rows = (static_cast<int>(albums.size()) + cols - 1) / cols;
    const float contentH = pad + rows * (cardH + gap);

    ui::ScrollArea(grid, contentH, &albumsScroll_);
    BeginScissorMode(static_cast<int>(grid.x), static_cast<int>(grid.y),
                     static_cast<int>(grid.width), static_cast<int>(grid.height));
    for (size_t i = 0; i < albums.size(); i++) {
        const int row = static_cast<int>(i) / cols, col = static_cast<int>(i) % cols;
        const float x = grid.x + pad + col * (cardW + gap);
        const float y = grid.y + 8 + row * (cardH + gap) - albumsScroll_;
        if (y + cardH < grid.y || y > grid.y + grid.height) continue;
        const Album& a = albums[i];
        const Rectangle card{x - 8, y - 8, cardW + 16, cardH + 16};
        if (ui::Hover(card) && ui::Hover(grid)) {
            DrawRectangleRounded(card, 0.08f, 6, ui::theme.elevated);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                detailAlbumId_ = a.id;
                detailScroll_ = 0;
                view_ = View::AlbumDetail;
            }
        }
        DrawAlbumArt(Rectangle{x, y, cardW, artH}, &a, 1.0f);
        ui::TextEllipsis(a.title, Vector2{x, y + artH + 8}, cardW, 15, ui::theme.text);
        const std::string sub = a.year > 0 ? TextFormat("%s · %d", a.artist.c_str(), a.year)
                                           : a.artist;
        ui::TextEllipsis(sub, Vector2{x, y + artH + 28}, cardW, 13, ui::theme.textSecondary);
    }
    EndScissorMode();
}

void App::DrawAlbumDetailView(Rectangle r) {
    const Album* album = library_.AlbumById(detailAlbumId_);
    if (album == nullptr) {
        view_ = View::Albums;
        return;
    }
    const auto tracks = library_.AlbumTracks(album->id);

    const Rectangle backR{r.x + 24, r.y + 18, 80, 24};
    ui::Text("< Albums", Vector2{backR.x, backR.y + 2}, 14,
             ui::Hover(backR) ? ui::theme.text : ui::theme.textSecondary);
    if (ui::Clicked(backR)) {
        view_ = View::Albums;
        return;
    }

    const float hy = r.y + 56;
    DrawAlbumArt(Rectangle{r.x + 24, hy, 160, 160}, album, 1.0f);
    const float tx = r.x + 24 + 160 + 24;
    ui::Text("ALBUM", Vector2{tx, hy + 8}, 12, ui::theme.textTertiary);
    ui::TextEllipsis(album->title, Vector2{tx, hy + 28}, r.width - tx - 24, 30, ui::theme.text);
    float dur = 0;
    for (const Track* t : tracks) dur += t->duration;
    std::string meta = album->artist;
    if (album->year > 0) meta += TextFormat(" · %d", album->year);
    meta += TextFormat(" · %d tracks, %s", static_cast<int>(tracks.size()),
                       ui::FormatTime(dur).c_str());
    ui::TextEllipsis(meta, Vector2{tx, hy + 70}, r.width - tx - 24, 14, ui::theme.textSecondary);

    const Rectangle playR{tx, hy + 108, 112, 38};
    DrawRectangleRounded(playR, 0.6f, 8,
                         ui::Hover(playR) ? Brighten(ui::theme.accent, 0.15f) : ui::theme.accent);
    ui::IconPlay(Vector2{playR.x + 28, playR.y + 19}, 13, ui::theme.bg);
    ui::Text("Play", Vector2{playR.x + 44, playR.y + 10}, 16, ui::theme.bg);
    if (ui::Clicked(playR) && !tracks.empty()) PlayFromTrackList(tracks, 0);

    const Rectangle table{r.x, hy + 176, r.width, r.height - (hy + 176 - r.y)};
    const int clicked = DrawTrackTable(table, tracks, &detailScroll_, false);
    if (clicked >= 0) PlayFromTrackList(tracks, clicked);
}

void App::DrawNowPlayingView(Rectangle r) {
    const Track* cur = player_.Current();
    if (cur == nullptr) {
        ui::IconNote(Vector2{r.x + r.width / 2, r.y + r.height / 2 - 30}, 56,
                     ui::theme.textTertiary);
        ui::TextCentered("Nothing playing", Vector2{r.x + r.width / 2, r.y + r.height / 2 + 24},
                         17, ui::theme.textSecondary);
        return;
    }
    const Album* album = library_.AlbumById(cur->albumId);
    // Art (and the layers on it) shows the vinyl-sequenced album, which lags
    // the current track during retract -> swap -> extend transitions.
    const Album* shownAlbum = library_.AlbumById(vinyl_.DisplayedAlbumId());
    if (shownAlbum == nullptr) shownAlbum = album;
    const Color glow = shownAlbum != nullptr ? shownAlbum->dominant : ui::theme.accent;
    const bool hasFg = fg_.tex.id != 0 && shownAlbum != nullptr && fg_.albumId == shownAlbum->id;
    const float artAlpha = vinyl_.ArtAlpha();

    // Ambient wash from the album's dominant color, breathing with the bass
    const float bass = visualizer_.BassLevel();
    DrawRectangleGradientV(static_cast<int>(r.x), static_cast<int>(r.y),
                           static_cast<int>(r.width), static_cast<int>(r.height * 0.75f),
                           Fade(glow, 0.10f + 0.10f * bass), Fade(glow, 0.0f));

    // With depth layers the visualizer lives inside the art, so give the
    // artwork the space the bottom bar strip used to take.
    const float visH = hasFg ? 0.0f : 170.0f;
    const float artSize = hasFg ? std::min({480.0f, r.height - 220, r.width - 200})
                                : std::min({380.0f, r.height - visH - 200, r.width - 160});
    const float artX = r.x + (r.width - artSize) / 2;
    const float artY = r.y + (hasFg ? 44 : 56);
    const Rectangle artRect{artX, artY, artSize, artSize};

    // Ambient glow: LED-underglow style — a tight bright line at the art edge
    // with a steep exponential falloff, not a wide soft wash
    const float glowBase = 0.16f + 0.20f * bass;
    constexpr float kGlowInflate[4] = {3, 6, 10, 16};
    constexpr float kGlowAlpha[4] = {1.0f, 0.45f, 0.18f, 0.06f};
    for (int i = 3; i >= 0; i--) {
        const float inflate = kGlowInflate[i];
        DrawRectangleRounded(Rectangle{artX - inflate, artY - inflate, artSize + 2 * inflate,
                                       artSize + 2 * inflate},
                             0.06f, 8, Fade(glow, glowBase * kGlowAlpha[i]));
    }

    if (config_.vinylDisc && shownAlbum != nullptr) {
        vinyl_.Draw(artRect, shownAlbum->accent, shownAlbum->dominant);
    }

    // Entrance animation: scale up and fade in around the art center
    const float s = vinyl_.ArtScale();
    const Rectangle shownRect{artRect.x + artRect.width * (1 - s) / 2,
                              artRect.y + artRect.height * (1 - s) / 2, artRect.width * s,
                              artRect.height * s};
    DrawAlbumArt(shownRect, shownAlbum, 1.4f, artAlpha);

    if (hasFg && vinyl_.ArtEntered()) {
        // The headline feature: bars play between the art and its subject.
        // Insets match the web's frame style (padX 6%, padBot 4%).
        BeginScissorMode(static_cast<int>(artRect.x), static_cast<int>(artRect.y),
                         static_cast<int>(artRect.width), static_cast<int>(artRect.height));
        visualizer_.DrawFullSurface(
            Rectangle{artRect.x + artSize * 0.06f, artRect.y, artSize * 0.88f, artSize * 0.96f},
            shownAlbum->accent, shownAlbum->hasSecondary ? &shownAlbum->accentSecondary : nullptr,
            0.65f);
        EndScissorMode();
        const float side = static_cast<float>(std::min(fg_.tex.width, fg_.tex.height));
        const Rectangle src{(fg_.tex.width - side) / 2, (fg_.tex.height - side) / 2, side, side};
        DrawTexturePro(fg_.tex, src, artRect, Vector2{0, 0}, 0, WHITE);
    }

    const float textY = artY + artSize + 30;
    ui::TextCentered(cur->title, Vector2{r.x + r.width / 2, textY}, 28, ui::theme.text);
    ui::TextCentered(cur->artist, Vector2{r.x + r.width / 2, textY + 34}, 17,
                     ui::theme.textSecondary);
    if (album != nullptr) {
        ui::TextCentered(album->title, Vector2{r.x + r.width / 2, textY + 60}, 14,
                         ui::theme.textTertiary);
    }

    if (!hasFg) {
        visualizer_.DrawBars(Rectangle{r.x + 32, r.y + r.height - visH - 8, r.width - 64, visH},
                             album != nullptr ? album->accent : Brighten(glow, 0.25f));
        if (config_.depthLayers && depth_.Busy()) {
            ui::TextCentered(TextFormat("preparing depth layers (%s)...", depth_.StatusText()),
                             Vector2{r.x + r.width / 2, r.y + r.height - visH - 28}, 13,
                             ui::theme.textTertiary);
        }
    }
}

void App::DrawEmptyState(Rectangle r) {
    const Vector2 c{r.x + r.width / 2, r.y + r.height / 2};
    ui::IconNote(Vector2{c.x, c.y - 70}, 64, ui::theme.textTertiary);
    ui::TextCentered("Your library is empty", Vector2{c.x, c.y}, 24, ui::theme.text);
    ui::TextCentered("Drop a music folder anywhere in this window to get started",
                     Vector2{c.x, c.y + 36}, 15, ui::theme.textSecondary);
    ui::TextCentered("MP3 · FLAC · OGG · WAV", Vector2{c.x, c.y + 64}, 13, ui::theme.textTertiary);
    if (library_.ScanActive()) {
        ui::TextCentered("Scanning...", Vector2{c.x, c.y + 100}, 14, ui::theme.accent);
    }
}

void App::DrawAlbumArt(Rectangle r, const Album* album, float iconScale, float alpha) {
    const Texture2D* tex = album != nullptr ? art_.Get(*album) : nullptr;
    if (tex != nullptr) {
        const float side = static_cast<float>(std::min(tex->width, tex->height));
        const Rectangle src{(tex->width - side) / 2, (tex->height - side) / 2, side, side};
        DrawTexturePro(*tex, src, r, Vector2{0, 0}, 0, Fade(WHITE, alpha));
    } else {
        DrawRectangleRounded(r, 0.06f, 6, Fade(ui::theme.elevated, alpha));
        ui::IconNote(Vector2{r.x + r.width / 2, r.y + r.height / 2},
                     std::min(r.width, r.height) * 0.3f * iconScale + 8,
                     Fade(ui::theme.textTertiary, alpha));
    }
    DrawRectangleLinesEx(r, 1, Fade(ui::theme.borderSubtle, alpha));
}

void App::DrawDebugOverlay() {
    const float W = static_cast<float>(GetScreenWidth());
    const float H = static_cast<float>(GetScreenHeight());
    const Rectangle box{W - 230, H - kPlayerH - 96, 218, 84};
    DrawRectangleRounded(box, 0.15f, 6, Fade(ui::theme.bg, 0.85f));
    DrawRectangleLinesEx(box, 1, ui::theme.border);
    const char* mode = eventWaiting_ ? "idle (event-wait)" : TextFormat("target %d fps", targetFps_);
    ui::Text(TextFormat("%d fps · %s", GetFPS(), mode), Vector2{box.x + 12, box.y + 10}, 13,
             ui::theme.text);
    ui::Text(TextFormat("frame %.2f ms", GetFrameTime() * 1000.0f), Vector2{box.x + 12, box.y + 28},
             13, ui::theme.textSecondary);
    ui::Text(TextFormat("%s · %.2f/%.2fs · bass %.2f",
                        player_.IsPlaying() ? "playing" : "stopped", player_.TimePlayed(),
                        player_.TimeLength(), visualizer_.BassLevel()),
             Vector2{box.x + 12, box.y + 44}, 13, ui::theme.textSecondary);
    ui::Text(TextFormat("depth: %s · fg %s", depth_.StatusText(),
                        fg_.tex.id != 0 ? "ready" : "none"),
             Vector2{box.x + 12, box.y + 62}, 13, ui::theme.textSecondary);
}
