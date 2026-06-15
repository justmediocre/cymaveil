#include "app.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>

#include "raymath.h"

#include "appearance.h"
#include "crashlog.h"
#include "icon_png.h"
#include "paths.h"
#include "ui.h"

namespace {

// Mirror every raylib/TraceLog line to a file in the data dir, flushed per
// line, so a hard crash (which never reaches a clean shutdown and can lose
// buffered stdout) still leaves a trail whose last line points at the culprit.
FILE* g_logFile = nullptr;
void FileTraceLog(int level, const char* text, va_list args) {
    (void)level;
    va_list copy;
    va_copy(copy, args);
    std::vprintf(text, args);
    std::putchar('\n');
    if (g_logFile != nullptr) {
        std::vfprintf(g_logFile, text, copy);
        std::fputc('\n', g_logFile);
        std::fflush(g_logFile);
    }
    va_end(copy);
}

constexpr float kSidebarW = 220.0f;
constexpr float kMiniPlayerH = 72.0f;
constexpr float kRowH = 44.0f;
constexpr float kQueueW = 320.0f;
constexpr float kQueueRowH = 52.0f;
// Pre-fire the vinyl retract this many seconds before a track's natural end so
// the disc is tucked away and the sleeve flip lands on the audio change. Equal
// to the normal retract duration in vinyl.cpp.
constexpr float kVinylLeadSeconds = 0.8f;
// Red heart for Favorites, matching the web app
constexpr Color kHeartRed{226, 85, 103, 255};

Color Brighten(Color c, float t) {
    return Color{static_cast<unsigned char>(c.r + (255 - c.r) * t),
                 static_cast<unsigned char>(c.g + (255 - c.g) * t),
                 static_cast<unsigned char>(c.b + (255 - c.b) * t), 255};
}

// The monitor the window mostly covers. raylib's GetCurrentMonitor() infers
// this from the window's center point and can fall back to the primary display;
// picking the largest-overlap monitor is steadier and copes with a window
// straddling two screens.
int MonitorForWindow() {
    const int count = GetMonitorCount();
    if (count <= 1) return 0;
    const Vector2 wp = GetWindowPosition();
    const float ww = static_cast<float>(GetScreenWidth());
    const float wh = static_cast<float>(GetScreenHeight());
    int best = 0;
    float bestArea = -1.0f;
    for (int i = 0; i < count; i++) {
        const Vector2 mp = GetMonitorPosition(i);
        const float mw = static_cast<float>(GetMonitorWidth(i));
        const float mh = static_cast<float>(GetMonitorHeight(i));
        const float ox = std::max(0.0f, std::min(wp.x + ww, mp.x + mw) - std::max(wp.x, mp.x));
        const float oy = std::max(0.0f, std::min(wp.y + wh, mp.y + mh) - std::max(wp.y, mp.y));
        const float area = ox * oy;
        if (area > bestArea) {
            bestArea = area;
            best = i;
        }
    }
    return best;
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
    // Unbuffered logging so a hard crash (e.g. a bad decode corrupting the heap)
    // doesn't swallow the last TraceLog lines that point at the culprit. Also
    // tee everything to <data dir>/cymaveil.log for when no console is attached.
    setvbuf(stdout, nullptr, _IONBF, 0);
    g_logFile = std::fopen((paths::DataDir() + "/cymaveil.log").c_str(), "w");
    SetTraceLogCallback(FileTraceLog);
    // Dump a symbolized stack trace to the log if the process crashes.
    crashlog::Install((paths::DataDir() + "/cymaveil.log").c_str());
    // Fixed-size in screenshot mode so tiling WMs float the window at the
    // requested resolution instead of fitting it into the layout, and no
    // HIGHDPI so shots come out at the exact requested pixel size on any
    // display. Interactively, HIGHDPI keeps layout in logical units: raylib
    // scales the framebuffer (Wayland) or window + mouse (X11) for us.
    SetConfigFlags(screenshotPath_.empty()
                       ? (FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI)
                       : FLAG_MSAA_4X_HINT);
    InitWindow(1280, 800, "Cymaveil");
    Image icon = LoadImageFromMemory(".png", kIconPng, kIconPngSize);
    if (icon.data != nullptr) {
        // GLFW only accepts R8G8B8A8 icons; no-op on Wayland (icon comes
        // from the desktop file there).
        ImageFormat(&icon, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        SetWindowIcon(icon);
        UnloadImage(icon);
    }
    // GLFW size limits are physical pixels on X11 but logical units on
    // Wayland; convert so the minimum stays 980x640 logical either way.
    // Backend check mirrors GLFW's own platform selection (platform.c).
#ifdef __linux__
    const char* session = std::getenv("XDG_SESSION_TYPE");
    const bool onWayland =
        std::getenv("WAYLAND_DISPLAY") != nullptr &&
        !(session != nullptr && std::strcmp(session, "x11") == 0 && std::getenv("DISPLAY") != nullptr);
#else
    const bool onWayland = false;  // Win32/Cocoa report physical pixels like X11
#endif
    const Vector2 dpi = GetWindowScaleDPI();  // {1,1} without FLAG_WINDOW_HIGHDPI
    SetWindowMinSize(static_cast<int>(980 * (onWayland ? 1.0f : dpi.x)),
                     static_cast<int>(640 * (onWayland ? 1.0f : dpi.y)));
#ifndef __linux__
    // GLFW leaves initial placement to the OS, and on HiDPI/multi-monitor
    // Windows the SCALE_TO_MONITOR growth can shove the window (title bar and
    // all) off-screen. Center it on its monitor in physical pixels — render
    // size and monitor size share units here — and clamp so the top-left never
    // lands above/left of the monitor. (Linux keeps its WM-driven placement;
    // SetWindowPosition is a no-op on Wayland anyway.)
    {
        const int mon = GetCurrentMonitor();
        const Vector2 mpos = GetMonitorPosition(mon);
        const int mx = static_cast<int>(mpos.x);
        const int my = static_cast<int>(mpos.y);
        int x = mx + (GetMonitorWidth(mon) - GetRenderWidth()) / 2;
        int y = my + (GetMonitorHeight(mon) - GetRenderHeight()) / 2;
        SetWindowPosition(x < mx ? mx : x, y < my ? my : y);
    }
#endif
    SetExitKey(KEY_NULL);  // ESC navigates, doesn't quit
    SetTargetFPS(targetFps_);
    InitAudioDevice();
    ui::Init();

    config_.Load();
    ApplyTheme();
    library_.Load();
    playlists_.Load();
    queueAnim_ = config_.queuePanel ? 1.0f : 0.0f;
    mosaic_.Rebuild(library_.Albums(), MosaicCfg());
    player_.SetVolume(config_.volume);
    player_.SetShuffle(config_.shuffle);
    player_.SetRepeat(static_cast<RepeatMode>(config_.repeat));
    player_.RestoreSession();
    visualizer_.Attach();
    if (config_.mpris) mpris_.Start();
    watcher_.Start();

    for (const auto& f : startupFolders_) {
        if (DirectoryExists(f.c_str())) library_.AddFolder(f);
    }
    // Watch the loaded + freshly-added folders so the library self-updates.
    SyncWatcher();
    if (startView_ == "library") view_ = View::Library;
    else if (startView_ == "search") view_ = View::Search;
    else if (startView_ == "albums") view_ = View::Albums;
    else if (startView_ == "playlists") view_ = View::Playlists;
    else if (startView_ == "now") view_ = View::NowPlaying;
    else if (startView_ == "settings") view_ = View::Settings;
    else if (startView_ == "brush") view_ = View::NowPlaying;  // editor opens once art loads
    // After the view flag: a successful import lands on the playlist detail
    for (const auto& f : startupImports_) ImportM3uFile(f);
    MarkActivity();

    while (!WindowShouldClose() && !quitRequested_) Frame();

    config_.volume = player_.Volume();
    config_.shuffle = player_.Shuffle();
    config_.repeat = static_cast<int>(player_.Repeat());
    config_.Save();
    player_.SaveSession();  // before Shutdown: needs the live play position

    mpris_.Stop();    // before CloseWindow: the worker pokes the GLFW event loop
    watcher_.Stop();  // ditto — it wakes the event loop on filesystem changes
    player_.Shutdown();
    visualizer_.Detach();
    mosaic_.Unload();
    backdrop_.Unload();
    vinyl_.Unload();
    sleeve3d_.Unload();
    albumGlow_.Unload();
    if (fg_.tex.id != 0) UnloadTexture(fg_.tex);
    if (brush_.artTex.id != 0) UnloadTexture(brush_.artTex);
    if (brush_.overlayTex.id != 0) UnloadTexture(brush_.overlayTex);
    art_.Clear();
    ui::Shutdown();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}

void App::Frame() {
    ui::NewFrame();
    HandleDroppedFolders();
    HandleInput();
    HandleMprisRequests();
    // The watcher fired: files moved on disk, so kick an (incremental) rescan.
    if (watcher_.Poll()) {
        library_.RequestRescan();
        MarkActivity();
    }
    player_.Update();
    if (library_.PollScan()) {
        mosaic_.Rebuild(library_.Albums(), MosaicCfg());
        backdrop_.MarkDirty();  // tiles now show different albums
        libGeneration_++;       // invalidate cached search results
        MarkActivity();
    }
    if (autoplay_ && !library_.Tracks().empty()) {
        autoplay_ = false;
        if (player_.HasTrack()) {
            // A restored session is already cued up; resume it instead.
            if (!player_.IsPlaying()) player_.TogglePause();
        } else {
            std::vector<const Track*> all;
            for (const auto& t : library_.Tracks()) all.push_back(&t);
            PlayFromTrackList(all, 0);
        }
        if (startView_.empty()) view_ = View::NowPlaying;
    }
    // Checkpoint the session while playing so a crash loses at most ~10s.
    if (player_.IsPlaying() && GetTime() - sessionSaveAt_ > 10.0) {
        sessionSaveAt_ = GetTime();
        player_.SaveSession();
    }
    visualizer_.Update(GetFrameTime(), player_.IsPlaying());
    mosaic_.Update(GetFrameTime(), player_.IsPlaying(), MosaicCfg());
    // Mark before the queue drains so textures decoded this frame (including
    // the final batch, after which HasPendingWork() is false) reach the backdrop.
    if (art_.HasPendingWork()) backdrop_.MarkDirty();
    art_.ProcessQueue(2);
    {
        const Track* cur = player_.Current();
        std::string targetAlbum = cur != nullptr ? cur->albumId : std::string{};
        // Pre-fire the vinyl transition so the disc retract + sleeve flip lines
        // up with the audio change rather than lagging a beat behind it. As a
        // playing track nears its natural end, peek at the auto-advance target;
        // if it's a different album, hand the vinyl that album now so it starts
        // retracting and the flip lands on the switch. Only with the disc shown:
        // without the retract to spend the lead, the flip would fire too early.
        if (config_.vinylDisc && player_.IsPlaying() && !seekDragging_ &&
            player_.Repeat() != RepeatMode::One) {
            const float remaining = player_.TimeLength() - player_.TimePlayed();
            if (remaining > 0.0f && remaining <= kVinylLeadSeconds &&
                player_.TimePlayed() > kVinylLeadSeconds) {
                const Track* next = player_.PeekNext();
                if (next != nullptr && next->albumId != targetAlbum) targetAlbum = next->albumId;
            }
        }
        vinyl_.Update(GetFrameTime(), targetAlbum, player_.IsPlaying(), manualSkip_,
                      config_.vinylDisc);
        manualSkip_ = false;
    }
    UpdateForeground();

    // Testing affordance: --view brush opens the editor on the shown album once
    // its art is available, so the overlay can be captured with --shot.
    if (startView_ == "brush" && !brush_.open) {
        const Album* a = library_.AlbumById(vinyl_.DisplayedAlbumId());
        if (a == nullptr || a->artPath.empty()) {
            for (const auto& al : library_.Albums())
                if (!al.artPath.empty()) {
                    a = &al;
                    break;
                }
        }
        if (a != nullptr && !a->artPath.empty()) {
            OpenBrushEditor(*a);
            startView_.clear();
        }
    }

    const float W = static_cast<float>(GetScreenWidth());
    const float H = static_cast<float>(GetScreenHeight());
    // Now Playing carries its own transport inline (like the web app); other
    // views get the compact mini player, or nothing when nothing is loaded.
    const bool miniBar = view_ != View::NowPlaying && player_.Current() != nullptr;
    const float barH = miniBar ? kMiniPlayerH : 0.0f;

    // Queue panel slide: content gives up the eased width on the right.
    const float queueTarget = config_.queuePanel ? 1.0f : 0.0f;
    if (queueAnim_ != queueTarget) {
        const float step = GetFrameTime() / 0.3f;
        queueAnim_ = Clamp(queueAnim_ + (queueAnim_ < queueTarget ? step : -step), 0.0f, 1.0f);
    }
    const float qe = queueAnim_ * queueAnim_ * (3.0f - 2.0f * queueAnim_);
    const float qw = kQueueW * qe;

    // Immersive Now Playing: in fullscreen the art goes full-bleed with the nav
    // sidebar tucked away, and the cursor fades out after a short idle — like the
    // web app's fullscreen mode. The queue panel still works (its toggle lives in
    // the transport). Other views keep their chrome even when fullscreen.
    const bool immersive = fullscreen_ && view_ == View::NowPlaying;
    const float sidebarW = immersive ? 0.0f : kSidebarW;
    if (!brush_.open) {
        const bool hide = immersive && GetTime() - lastActivity_ > 2.5;
        if (hide && !cursorHidden_) {
            HideCursor();
            cursorHidden_ = true;
        } else if (!hide && cursorHidden_) {
            ShowCursor();
            cursorHidden_ = false;
        }
    }

    const Rectangle sidebar{0, 0, sidebarW, H - barH};
    const Rectangle content{sidebarW, 0, W - sidebarW - qw, H - barH};
    const Rectangle queuePanel{W - qw, 0, qw, H - barH};
    const Rectangle bar{0, H - barH, W, barH};

    // The context menu and brush editor overlay everything; swallow the mouse
    // underneath them so widgets in the views don't react through the overlay.
    ui::BlockInput(menu_.open || brush_.open);

    // Render the mosaic offscreen so the chrome panels can sample a blurred copy
    // of it (frosted glass). The sharp copy is the screen's base layer. The
    // offscreen render and two-pass blur are the frame's heaviest GPU work, so we
    // only redo them when the mosaic actually changed: tiles animating, art still
    // decoding (tiles swap in fresh textures as it lands), a theme switch (handled
    // in ApplyTheme), or a resize (handled in EnsureSize). Otherwise the cached
    // scene_/blur_ textures are still valid and we reuse them untouched.
    backdrop_.EnsureSize(static_cast<int>(W), static_cast<int>(H));
    if (mosaic_.Animating()) backdrop_.MarkDirty();
    if (backdrop_.NeedsRender()) {
        backdrop_.BeginScene();
        ClearBackground(ui::theme.bg);
        mosaic_.Draw(Rectangle{0, 0, W, H}, art_, library_, MosaicCfg(), ui::theme.bg);
        backdrop_.EndScene();
    }

    // Render the turning sleeve into its offscreen 3D target before the 2D pass
    // (like the backdrop). Needs both covers resident; otherwise Now Playing
    // falls back to the flat squish for the frame.
    if (view_ == View::NowPlaying) {
        const Album* shown = library_.AlbumById(vinyl_.DisplayedAlbumId());
        const Texture2D* cover = shown != nullptr ? art_.Get(*shown) : nullptr;
        if (cover != nullptr) albumGlow_.Update(*cover, shown->id);
    }
    flip3dReady_ = false;
    if (view_ == View::NowPlaying && vinyl_.Flipping()) {
        const Album* fromA = library_.AlbumById(vinyl_.FlipFrom());
        const Album* toA = library_.AlbumById(vinyl_.FlipTo());
        const Texture2D* fromT = fromA != nullptr ? art_.Get(*fromA) : nullptr;
        const Texture2D* toT = toA != nullptr ? art_.Get(*toA) : nullptr;
        if (fromT != nullptr && toT != nullptr) {
            sleeve3d_.Render(fromT, toT, fromA->dominant, toA->dominant, vinyl_.FlipProgress());
            flip3dReady_ = true;
        }
    }

    BeginDrawing();
    ClearBackground(ui::theme.bg);
    backdrop_.DrawScene();

    switch (view_) {
        case View::Search: DrawSearchView(content); break;
        case View::Library: DrawLibraryView(content); break;
        case View::Albums: DrawAlbumsView(content); break;
        case View::AlbumDetail: DrawAlbumDetailView(content); break;
        case View::Playlists: DrawPlaylistsView(content); break;
        case View::PlaylistDetail: DrawPlaylistDetailView(content); break;
        case View::NowPlaying: DrawNowPlayingView(content); break;
        case View::Settings: DrawSettingsView(content); break;
    }
    if (!immersive) DrawSidebar(sidebar);
    if (qw > 0.5f) DrawQueuePanel(queuePanel);
    if (miniBar) DrawMiniPlayer(bar);
    if (brush_.open) DrawBrushEditor(Rectangle{0, 0, W, H});
    DrawTrackMenu();
    DrawToast(barH);
    if (showDebug_) DrawDebugOverlay(barH);

    EndDrawing();
    ui::BlockInput(false);

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
        quitRequested_ = true;
    }
    PublishMpris();
    UpdatePacing();
}

void App::HandleMprisRequests() {
    MprisRequest req;
    while (mpris_.PollRequest(&req)) {
        MarkActivity();
        switch (req.cmd) {
            case MprisCommand::Raise: SetWindowFocused(); break;
            case MprisCommand::Quit: quitRequested_ = true; break;
            case MprisCommand::Next:
                player_.Next();
                manualSkip_ = true;
                break;
            case MprisCommand::Previous:
                player_.Prev();
                manualSkip_ = true;
                break;
            case MprisCommand::Pause:
                if (player_.IsPlaying()) player_.TogglePause();
                break;
            case MprisCommand::PlayPause: player_.TogglePause(); break;
            case MprisCommand::Stop: player_.Stop(); break;
            case MprisCommand::Play:
                if (!player_.IsPlaying()) player_.TogglePause();
                break;
            case MprisCommand::SeekBy: {
                const float target = player_.TimePlayed() + static_cast<float>(req.value);
                // Per spec: seeking past the end acts like Next
                if (player_.HasTrack() && target >= player_.TimeLength()) {
                    player_.Next();
                    manualSkip_ = true;
                } else {
                    player_.SeekTo(target);
                }
                break;
            }
            case MprisCommand::SetPosition: {
                const Track* cur = player_.Current();
                if (cur != nullptr && cur->id == req.str) {
                    player_.SeekTo(static_cast<float>(req.value));
                }
                break;
            }
            case MprisCommand::SetVolume:
                player_.SetVolume(static_cast<float>(req.value));
                break;
            case MprisCommand::SetShuffle:
                if (player_.Shuffle() != (req.value != 0)) player_.ToggleShuffle();
                break;
            case MprisCommand::SetLoop:
                if (req.str == "None") player_.SetRepeat(RepeatMode::Off);
                else if (req.str == "Playlist") player_.SetRepeat(RepeatMode::All);
                else if (req.str == "Track") player_.SetRepeat(RepeatMode::One);
                break;
        }
    }
}

void App::PublishMpris() {
    if (!mpris_.Active()) return;
    MprisState s;
    s.hasTrack = player_.HasTrack();
    s.hasQueue = player_.QueueSize() > 0;
    s.status = player_.IsPlaying() ? "Playing"
               : (player_.HasTrack() && !player_.IsStopped()) ? "Paused"
                                                              : "Stopped";
    if (const Track* cur = player_.Current(); cur != nullptr) {
        s.trackId = cur->id;
        s.title = cur->title;
        s.artist = cur->artist;
        s.lengthUs = static_cast<long long>(cur->duration * 1e6);
        if (const Album* a = library_.AlbumById(cur->albumId); a != nullptr) {
            s.album = a->title;
            if (!a->artPath.empty()) s.artUrl = MprisFileUrl(a->artPath);
        }
    }
    s.shuffle = player_.Shuffle();
    s.loop = player_.Repeat() == RepeatMode::All    ? "Playlist"
             : player_.Repeat() == RepeatMode::One ? "Track"
                                                   : "None";
    s.volume = player_.Volume();
    mpris_.Publish(s, player_.TimePlayed(), GetFrameTime());
}

void App::HandleInput() {
    const Vector2 d = GetMouseDelta();
    if (d.x != 0 || d.y != 0 || GetMouseWheelMove() != 0 || IsMouseButtonDown(MOUSE_BUTTON_LEFT) ||
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        MarkActivity();
    }
    while (GetKeyPressed() != 0) MarkActivity();  // drain queue: any key wakes the UI

    // The brush editor consumes all input while open (handled in its draw pass).
    if (brush_.open) return;

    // While renaming a playlist every key belongs to the text box.
    if (!editPlaylistId_.empty()) return;
    // Likewise while typing a path into the Settings folder field.
    if (view_ == View::Settings && folderInputActive_) {
        if (IsKeyPressed(KEY_ESCAPE)) folderInputActive_ = false;
        return;
    }
    if (menu_.open && IsKeyPressed(KEY_ESCAPE)) {
        menu_.open = false;
        ui::ConsumeKey(KEY_ESCAPE);  // don't let the Search input re-read this press
        return;
    }

    // F11 toggles fullscreen anywhere; Esc backs out of it first (before the
    // per-view "back" handlers below), mirroring the web app's shortcuts.
    if (IsKeyPressed(KEY_F11)) {
        ToggleFullscreenMode();
        return;
    }
    if (fullscreen_ && IsKeyPressed(KEY_ESCAPE)) {
        ToggleFullscreenMode();
        ui::ConsumeKey(KEY_ESCAPE);
        return;
    }

    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl && IsKeyPressed(KEY_F)) {
        view_ = View::Search;
        return;
    }
    // On the Search view the always-focused input owns every printable key, so
    // any shortcut bound to a bare letter/digit/comma/space would steal a
    // keystroke from the box. Those stay gated behind `!onSearch`; arrow keys,
    // modifier combos, and function keys produce no text the box reads, so they
    // keep working as global shortcuts here too.
    const bool onSearch = view_ == View::Search;

    if (IsKeyPressed(KEY_SPACE) && !onSearch) player_.TogglePause();
    if (IsKeyPressed(KEY_RIGHT)) {
        if (ctrl) {
            player_.Next();
            manualSkip_ = true;
        } else if (!onSearch) {
            player_.SeekTo(player_.TimePlayed() + 5.0f);
        }
    }
    if (IsKeyPressed(KEY_LEFT)) {
        if (ctrl) {
            player_.Prev();
            manualSkip_ = true;
        } else if (!onSearch) {
            player_.SeekTo(player_.TimePlayed() - 5.0f);
        }
    }
    if (IsKeyPressed(KEY_UP)) player_.SetVolume(player_.Volume() + 0.05f);
    if (IsKeyPressed(KEY_DOWN)) player_.SetVolume(player_.Volume() - 0.05f);
    if (IsKeyPressed(KEY_S) && !onSearch) player_.ToggleShuffle();
    if (IsKeyPressed(KEY_R) && !onSearch) player_.CycleRepeat();
    if (IsKeyPressed(KEY_Q) && !onSearch) ToggleQueuePanel();
    // X opens the mask brush editor for the album on screen (same as the
    // brush button in Now Playing). Inside the editor X toggles paint/erase.
    // The Esc handler above closes an open track menu first, so X must also
    // defer to it rather than stack the editor under the live menu.
    if (IsKeyPressed(KEY_X) && !menu_.open && !onSearch) {
        ui::ConsumeKey(KEY_X);  // don't let DrawBrushEditor re-read this press
        const Track* cur = player_.Current();
        const Album* shown = library_.AlbumById(vinyl_.DisplayedAlbumId());
        if (shown == nullptr && cur != nullptr) shown = library_.AlbumById(cur->albumId);
        if (shown != nullptr && !shown->artPath.empty()) OpenBrushEditor(*shown);
    }
    if (!onSearch) {
        if (IsKeyPressed(KEY_ONE)) view_ = View::Library;
        if (IsKeyPressed(KEY_TWO)) view_ = View::Albums;
        if (IsKeyPressed(KEY_THREE)) view_ = View::Playlists;
        if (IsKeyPressed(KEY_FOUR)) view_ = View::NowPlaying;
        if (IsKeyPressed(KEY_COMMA)) view_ = View::Settings;  // common "preferences" shortcut
    }
    if (IsKeyPressed(KEY_ESCAPE) && view_ == View::AlbumDetail) view_ = View::Albums;
    if (IsKeyPressed(KEY_ESCAPE) && view_ == View::PlaylistDetail) view_ = View::Playlists;
    if (IsKeyPressed(KEY_ESCAPE) && view_ == View::Settings) view_ = View::Library;
    if (IsKeyPressed(KEY_F3)) showDebug_ = !showDebug_;
    if (IsKeyPressed(KEY_B) && !onSearch) mosaic_.Trigger(MosaicCfg());  // manually animate a tile
}

MosaicSettings App::MosaicCfg() const {
    return MosaicSettings{config_.mosaicEnabled, config_.mosaicOpacity, config_.mosaicDensity,
                          config_.mosaicTransition, config_.mosaicFlat};
}

void App::ApplyTheme() {
    bool light = config_.theme == "light";
    if (config_.theme == "system") {
        light = appearance::SystemScheme() == appearance::Scheme::Light;
    }
    ui::ApplyTheme(light);
    // The mosaic and clear color are tinted by the palette, so the cached
    // backdrop scene/blur no longer match — force a re-render next frame.
    backdrop_.MarkDirty();
    MarkActivity();
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

// ── Manual mask painting (brush editor) ──

void App::OpenBrushEditor(const Album& album) {
    if (album.artPath.empty()) return;
    // Never open the editor under a live context menu: the menu draws on top,
    // and its raw mouse reads would otherwise both activate a row and stamp
    // paint into the canvas underneath on the same click.
    menu_.open = false;
    const std::string maskPath = DepthEngine::MaskPath(album.id);
    // Seed from the existing mask when there is one, else start from a blank
    // (all-background) canvas the user paints the in-front subject onto.
    if (!brush_.canvas.Init(album.artPath, DepthEngine::MaskExists(album.id) ? maskPath : "", 256)) {
        Toast("Couldn't load artwork");
        return;
    }
    brush_.albumId = album.id;

    if (brush_.artTex.id != 0) UnloadTexture(brush_.artTex);
    brush_.artTex = LoadTexture(album.artPath.c_str());
    SetTextureFilter(brush_.artTex, TEXTURE_FILTER_BILINEAR);

    brush_.overlayDirty = true;
    RebuildBrushOverlay();  // creates overlayTex
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
    const Album* album = library_.AlbumById(brush_.albumId);
    if (album == nullptr) {
        CloseBrushEditor();
        return;
    }
    if (!brush_.canvas.Save(DepthEngine::MaskPath(album->id))) {
        Toast("Couldn't save mask");
        return;
    }
    // A hand-painted mask is only visible with depth layers on; turn them on so
    // the work shows immediately rather than silently doing nothing.
    if (!config_.depthLayers) {
        config_.depthLayers = true;
        config_.Save();
    }
    // Force the live foreground to pick up the new mask on the next frame, and
    // build it now for instant feedback if this album is the one on screen.
    fg_.albumId.clear();
    fg_.checkedAlbum.clear();
    BuildForeground(*album);
    Toast("Mask saved");
    CloseBrushEditor();
}

void App::DrawBrushEditor(Rectangle r) {
    if (brush_.overlayDirty) RebuildBrushOverlay();

    // Opaque backdrop covering the chrome underneath.
    DrawRectangleRec(r, ui::theme.bg);

    const Album* album = library_.AlbumById(brush_.albumId);
    const Color accent = album != nullptr ? album->accent : ui::theme.accent;

    // ── Geometry: a centred square canvas with the toolbar below it ──
    const float toolbarH = 46, gap = 18;
    const float canvasSize = std::min({720.0f, r.width - 120, r.height - 200});
    const float cx = r.x + r.width / 2;
    const float blockH = canvasSize + gap + toolbarH;
    const float top = r.y + std::max(48.0f, (r.height - blockH) / 2);
    const Rectangle artRect{cx - canvasSize / 2, top, canvasSize, canvasSize};

    // Title above the canvas.
    if (album != nullptr) {
        ui::TextCentered("Editing mask · " + album->title,
                         Vector2{cx, std::max(r.y + 22, top - 24)}, 14, ui::theme.textSecondary);
    }

    // ── Layer 0: album art ──
    if (brush_.artTex.id != 0) {
        const float side = static_cast<float>(std::min(brush_.artTex.width, brush_.artTex.height));
        const Rectangle src{(brush_.artTex.width - side) / 2, (brush_.artTex.height - side) / 2, side,
                            side};
        DrawTexturePro(brush_.artTex, src, artRect, Vector2{0, 0}, 0, WHITE);
    }
    // ── Layer 1: visualizer, between the art and the mask (same inset as NP) ──
    BeginScissorMode(static_cast<int>(artRect.x), static_cast<int>(artRect.y),
                     static_cast<int>(artRect.width), static_cast<int>(artRect.height));
    visualizer_.DrawFullSurface(
        Rectangle{artRect.x + canvasSize * 0.06f, artRect.y, canvasSize * 0.88f, canvasSize * 0.96f},
        accent, album != nullptr && album->hasSecondary ? &album->accentSecondary : nullptr, 0.65f);
    EndScissorMode();
    // ── Layer 2: mask preview composite ──
    if (brush_.overlayTex.id != 0) {
        DrawTexturePro(brush_.overlayTex,
                       Rectangle{0, 0, static_cast<float>(brush_.overlayTex.width),
                                 static_cast<float>(brush_.overlayTex.height)},
                       artRect, Vector2{0, 0}, 0, WHITE);
    }
    DrawRectangleLinesEx(artRect, 1, Fade(ui::theme.text, 0.08f));

    // ── Painting: mouse → mask-space, with brush stroke interpolation ──
    // The editor draws beneath the context menu, so suppress canvas strokes
    // while a menu is up — otherwise a click on a menu row over the canvas
    // would both activate the row and stamp paint here (both use raw reads).
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
        const Color ring = brush_.canvas.mode() == BrushCanvas::Mode::Paint
                               ? Color{255, 255, 255, 200}
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
    const float widths[] = {wPaint, wErase, wMinus, wSize, wPlus,
                            wUndo,  wRedo,  wReset, wSave, wClose};
    constexpr int kNumItems = 10;
    float total = 2 * padIn + g * (kNumItems - 1);
    for (float w : widths) total += w;
    const float ty = artRect.y + canvasSize + gap;
    const Rectangle panel{cx - total / 2, ty, total, toolbarH};
    DrawRectangleRounded(panel, 0.5f, 10, ui::theme.surface);
    DrawRectangleRoundedLinesEx(panel, 0.5f, 10, 1, ui::theme.borderSubtle);

    float bx = panel.x + padIn;
    const float by = ty + (toolbarH - bh) / 2;
    const auto place = [&](float w) {
        const Rectangle b{bx, by, w, bh};
        bx += w + g;
        return b;
    };
    // A labelled button; `active` paints the accent state, `enabled=false` dims.
    const auto button = [&](Rectangle b, const char* label, bool active, bool enabled) {
        const bool hov = enabled && ui::HoverRaw(b);
        if (active) {
            DrawRectangleRounded(b, 0.35f, 6, Fade(accent, 0.18f));
            DrawRectangleRoundedLinesEx(b, 0.35f, 6, 1, accent);
        } else if (hov) {
            DrawRectangleRounded(b, 0.35f, 6, ui::theme.elevated);
        }
        const Color col = !enabled    ? ui::theme.textTertiary
                          : active    ? accent
                          : hov       ? ui::theme.text
                                      : ui::theme.textSecondary;
        ui::TextCentered(label, Vector2{b.x + b.width / 2, b.y + b.height / 2}, 13, col);
        return enabled && ui::ClickedRaw(b);
    };

    const bool isPaint = brush_.canvas.mode() == BrushCanvas::Mode::Paint;
    if (button(place(wPaint), "Paint", isPaint, true)) brush_.canvas.SetMode(BrushCanvas::Mode::Paint);
    if (button(place(wErase), "Erase", !isPaint, true)) brush_.canvas.SetMode(BrushCanvas::Mode::Erase);
    if (button(place(wMinus), "\xE2\x88\x92", false, true))  // minus sign
        brush_.canvas.SetRadius(brush_.canvas.Radius() - 1);
    {
        const Rectangle b = place(wSize);
        ui::TextCentered(TextFormat("Size %d", brush_.canvas.Radius()),
                         Vector2{b.x + b.width / 2, b.y + b.height / 2}, 13, ui::theme.text);
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
        DrawRectangleRounded(b, 0.35f, 6, hov ? Brighten(accent, 0.12f) : accent);
        ui::TextCentered("Save", Vector2{b.x + b.width / 2, b.y + b.height / 2}, 13, ui::theme.bg);
        if (ui::ClickedRaw(b)) {
            SaveBrushMask();
            return;
        }
    }
    {
        const Rectangle b = place(wClose);
        const bool hov = ui::HoverRaw(b);
        if (hov) DrawRectangleRounded(b, 0.4f, 6, ui::theme.elevated);
        ui::IconClose(Vector2{b.x + b.width / 2, b.y + b.height / 2}, 14,
                      hov ? ui::theme.text : ui::theme.textSecondary);
        if (ui::ClickedRaw(b)) {
            CloseBrushEditor();
            return;
        }
    }

    // Keyboard hint line.
    ui::TextCentered("X paint/erase    [ ] size    Ctrl+Scroll size    Ctrl+Z/Y undo    "
                     "Ctrl+S save    Esc close",
                     Vector2{cx, ty + toolbarH + 18}, 11, ui::theme.textTertiary);
}

void App::HandleDroppedFolders() {
    if (!IsFileDropped()) return;
    FilePathList files = LoadDroppedFiles();
    for (unsigned int i = 0; i < files.count; i++) {
        const char* p = files.paths[i];
        std::string ext = std::filesystem::path(p).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ext == ".m3u" || ext == ".m3u8") {
            ImportM3uFile(p);
        } else if (DirectoryExists(p)) {
            library_.AddFolder(p);
        } else {
            // A dropped file adds its containing folder
            const std::string parent = std::filesystem::path(p).parent_path().string();
            if (!parent.empty() && DirectoryExists(parent.c_str())) library_.AddFolder(parent);
        }
    }
    UnloadDroppedFiles(files);
    SyncWatcher();  // a dropped folder may have changed the watched set
    MarkActivity();
}

void App::UpdatePacing() {
    // The whole point of the rewrite: only burn CPU/GPU when something moves.
    //  - playing + focused: 60 fps for the visualizer
    //  - playing + unfocused: 30 fps (stream still needs feeding)
    //  - recent input / scan / pending art decodes: 60 fps
    //  - otherwise: block on OS events (near-zero usage until input arrives)
    const bool busy = library_.ScanActive() || art_.HasPendingWork() || seekDragging_ ||
                      volumeDragging_ || mosaic_.Animating() || depth_.Busy() ||
                      vinyl_.Animating() || brush_.open || ui::MarqueeActive() ||
                      queueAnim_ != (config_.queuePanel ? 1.0f : 0.0f) ||
                      !editPlaylistId_.empty() ||  // caret blink
                      GetTime() < toastUntil_;
    const bool recentInput = GetTime() - lastActivity_ < 2.5;

    int fps;
    bool wait = false;
    if (player_.IsPlaying()) {
        fps = IsWindowFocused() ? 60 : 30;
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

void App::ToggleFullscreenMode() {
    // Use raylib's fullscreen toggle rather than borderless-windowed: on X11 the
    // borderless path re-decorates the window *before* detaching it from the
    // monitor, so the title bar / min-max-close buttons never come back on exit.
    // ToggleFullscreen() re-parents to a windowed state first and then restores
    // GLFW_DECORATED, which is the order X11 actually honours. It targets the
    // monitor's current video mode (GLFW_DONT_CARE refresh), so there's no
    // resolution switch — it stays at the desktop resolution.
    if (!fullscreen_) {
        // Pin the monitor the window is actually on. Under X11 GetCurrentMonitor()
        // can land on the primary display, so we reassert the correct one from the
        // window rectangle. Under Wayland the window position is unavailable, so
        // detection collapses to monitor 0 (no correction) and a raylib/GLFW patch
        // makes the compositor fullscreen on the window's current output instead.
        const int monitor = MonitorForWindow();
        ToggleFullscreen();
        if (IsWindowFullscreen() && GetCurrentMonitor() != monitor) SetWindowMonitor(monitor);
    } else {
        ToggleFullscreen();
    }
    fullscreen_ = IsWindowFullscreen();
    // Reveal the cursor immediately on the way out (and reset the idle timer so
    // it lingers for a moment on the way in).
    if (cursorHidden_) {
        ShowCursor();
        cursorHidden_ = false;
    }
    MarkActivity();
}

void App::DrawSidebar(Rectangle r) {
    backdrop_.DrawGlass(r, Fade(ui::theme.surface, 0.72f));
    DrawLineEx(Vector2{r.width, 0}, Vector2{r.width, r.height}, 1, ui::theme.borderSubtle);

    ui::Text("Cymaveil", Vector2{20, 22}, 26, ui::theme.accent);

    struct NavItem {
        const char* label;
        View view;
    };
    const NavItem items[] = {{"Search", View::Search},
                             {"Library", View::Library},
                             {"Albums", View::Albums},
                             {"Playlists", View::Playlists},
                             {"Now Playing", View::NowPlaying},
                             {"Settings", View::Settings}};
    float y = 78;
    for (const auto& item : items) {
        const Rectangle row{10, y, r.width - 20, 38};
        const bool active = view_ == item.view ||
                            (item.view == View::Albums && view_ == View::AlbumDetail) ||
                            (item.view == View::Playlists && view_ == View::PlaylistDetail);
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

void App::DrawMiniPlayer(Rectangle r) {
    const Track* cur = player_.Current();
    if (cur == nullptr) return;
    const Album* album = library_.AlbumById(cur->albumId);

    backdrop_.DrawGlass(r, Fade(ui::theme.surface, 0.78f));
    DrawLineEx(Vector2{r.x, r.y}, Vector2{r.x + r.width, r.y}, 1, ui::theme.borderSubtle);

    const float cy = r.y + r.height / 2;
    const Rectangle queueR{r.x + r.width - 152, cy - 20, 40, 40};
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
    const float infoW = queueR.x - (r.x + 80) - 12;
    ui::TextEllipsis(cur->title, Vector2{r.x + 80, cy - 18}, infoW, 15, ui::theme.text);
    ui::TextEllipsis(cur->artist, Vector2{r.x + 80, cy + 2}, infoW, 12, ui::theme.textTertiary);

    // Queue toggle + play/pause + next
    if (ui::Hover(queueR)) DrawCircleV(Vector2{queueR.x + 20, cy}, 20, ui::theme.hover);
    ui::IconQueue(Vector2{queueR.x + 20, cy}, 14,
                  config_.queuePanel ? ui::theme.accent : ui::theme.textSecondary);
    if (ui::Clicked(queueR)) ToggleQueuePanel();

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
    if (ui::Clicked(r) && !ui::Hover(playR) && !ui::Hover(nextR) && !ui::Hover(queueR) &&
        !ui::Hover(progressHit)) {
        view_ = View::NowPlaying;
    }
}

App::TableResult App::DrawTrackTable(Rectangle r, const std::vector<const Track*>& tracks,
                                     float* scroll, bool showAlbum, bool removable) {
    const float pad = 24;
    const float numW = 44, durW = 64;
    const float artW = 32, artGap = 12;
    const float removeW = removable ? 28 : 0;
    const float flexW = r.width - pad * 2 - numW - artW - artGap - durW - removeW;
    const float titleW = flexW * (showAlbum ? 0.42f : 0.72f);
    const float artistW = flexW * 0.28f;
    const float albumW = showAlbum ? flexW * 0.30f : 0;

    // Header
    const float hx = r.x + pad;
    const float titleX = hx + numW + artW + artGap;
    ui::Text("#", Vector2{hx, r.y + 10}, 12, ui::theme.textTertiary);
    ui::Text("TITLE", Vector2{titleX, r.y + 10}, 12, ui::theme.textTertiary);
    ui::Text("ARTIST", Vector2{titleX + titleW, r.y + 10}, 12, ui::theme.textTertiary);
    if (showAlbum) {
        ui::Text("ALBUM", Vector2{titleX + titleW + artistW, r.y + 10}, 12,
                 ui::theme.textTertiary);
    }
    ui::TextRight("TIME", Vector2{r.x + r.width - pad - removeW, r.y + 10}, 12,
                  ui::theme.textTertiary);
    DrawLineEx(Vector2{r.x + pad, r.y + 32}, Vector2{r.x + r.width - pad, r.y + 32}, 1,
               ui::theme.borderSubtle);

    const Rectangle list{r.x, r.y + 36, r.width, r.height - 36};
    ui::ScrollArea(list, static_cast<float>(tracks.size()) * kRowH, scroll);

    TableResult out;
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
            bool overRemove = false;
            if (removable) {
                const Rectangle xR{r.x + r.width - pad - 20, y + kRowH / 2 - 10, 20, 20};
                overRemove = ui::Hover(xR);
                ui::IconClose(Vector2{xR.x + 10, xR.y + 10}, 14,
                              overRemove ? ui::theme.text : ui::theme.textTertiary);
                if (overRemove && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) out.removed = i;
            }
            if (!overRemove && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) out.clicked = i;
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) out.rightClicked = i;
        }
        const float ty = y + 13;
        const Color titleCol = isCurrent ? ui::theme.accent : ui::theme.text;
        if (isCurrent) {
            ui::IconNote(Vector2{hx + 8, y + kRowH / 2}, 16, ui::theme.accent);
        } else {
            ui::Text(t.trackNum > 0 ? TextFormat("%d", t.trackNum) : "-", Vector2{hx, ty}, 15,
                     ui::theme.textTertiary);
        }
        const Album* a = library_.AlbumById(t.albumId);
        DrawAlbumArt(Rectangle{hx + numW, y + (kRowH - artW) / 2, artW, artW}, a, 0.7f);
        ui::TextEllipsis(t.title, Vector2{titleX, ty}, titleW - 16, 15, titleCol);
        ui::TextEllipsis(t.artist, Vector2{titleX + titleW, ty}, artistW - 16, 14,
                         ui::theme.textSecondary);
        if (showAlbum) {
            ui::TextEllipsis(a != nullptr ? a->title : "", Vector2{titleX + titleW + artistW, ty},
                             albumW - 16, 14, ui::theme.textSecondary);
        }
        ui::TextRight(ui::FormatTime(t.duration), Vector2{r.x + r.width - pad - removeW, ty}, 14,
                      ui::theme.textSecondary);
    }
    EndScissorMode();
    return out;
}

void App::PlayFromTrackList(const std::vector<const Track*>& list, int index, QueueSource source,
                            std::string sourceId) {
    std::vector<std::string> ids;
    ids.reserve(list.size());
    for (const Track* t : list) ids.push_back(t->id);
    player_.PlayQueue(std::move(ids), index, source, std::move(sourceId));
    manualSkip_ = true;
    MarkActivity();
}

void App::ShufflePlay(const std::vector<const Track*>& list, QueueSource source,
                      std::string sourceId) {
    if (list.empty()) return;
    player_.SetShuffle(true);
    PlayFromTrackList(list, GetRandomValue(0, static_cast<int>(list.size()) - 1), source,
                      std::move(sourceId));
}

bool App::ShuffleButton(float* x, float y) {
    const Rectangle b{*x, y, 36 + ui::Measure("Shuffle", 13).x + 16, 30};
    *x += b.width + 10;
    if (ui::Hover(b)) DrawRectangleRounded(b, 0.6f, 8, Fade(ui::theme.hover, 0.7f));
    DrawRectangleRoundedLinesEx(b, 0.6f, 8, 1, ui::theme.border);
    const Color col = ui::Hover(b) ? ui::theme.text : ui::theme.textSecondary;
    ui::IconShuffle(Vector2{b.x + 22, b.y + 15}, 12, col);
    ui::Text("Shuffle", Vector2{b.x + 36, b.y + 8}, 13, col);
    return ui::Clicked(b);
}

bool App::ShuffleAllButton(Rectangle r) {
    const float w = 38 + ui::Measure("Shuffle All", 13).x + 18;
    const Rectangle b{r.x + r.width - 24 - w, r.y + 28, w, 36};
    DrawRectangleRounded(b, 0.5f, 8, Fade(ui::theme.accent, ui::Hover(b) ? 0.3f : 0.18f));
    ui::IconShuffle(Vector2{b.x + 24, b.y + 18}, 13, ui::theme.accent);
    ui::Text("Shuffle All", Vector2{b.x + 38, b.y + 11}, 13, ui::theme.accent);
    return ui::Clicked(b);
}

std::vector<const Track*> App::ResolveTracks(const std::vector<std::string>& ids) const {
    std::vector<const Track*> out;
    out.reserve(ids.size());
    for (const auto& id : ids) {
        if (const Track* t = library_.TrackById(id)) out.push_back(t);
    }
    return out;
}

void App::DrawAlbumGrid(Rectangle r, const std::vector<const Album*>& albums, float cardW,
                        float artH, float gap, float titleSize, float subSize, bool showYear,
                        float* scroll) {
    const float pad = 24;
    const float cardH = artH + 50;
    const int cols = std::max(1, static_cast<int>((r.width - pad * 2 + gap) / (cardW + gap)));
    const int rows = (static_cast<int>(albums.size()) + cols - 1) / cols;
    const float contentH = rows * (cardH + gap) + 8;

    ui::ScrollArea(r, contentH, scroll);
    BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width),
                     static_cast<int>(r.height));
    for (size_t i = 0; i < albums.size(); i++) {
        const int row = static_cast<int>(i) / cols, col = static_cast<int>(i) % cols;
        const float x = r.x + pad + col * (cardW + gap);
        const float y = r.y + 8 + row * (cardH + gap) - *scroll;
        if (y + cardH < r.y || y > r.y + r.height) continue;
        const Album& a = *albums[i];
        const Rectangle card{x - 8, y - 8, cardW + 16, cardH + 16};
        if (ui::Hover(card) && ui::Hover(r)) {
            DrawRectangleRounded(card, 0.08f, 6, ui::theme.elevated);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                detailAlbumId_ = a.id;
                detailScroll_ = 0;
                view_ = View::AlbumDetail;
            }
        }
        DrawAlbumArt(Rectangle{x, y, cardW, artH}, &a, 1.0f);
        ui::TextEllipsis(a.title, Vector2{x, y + artH + 8}, cardW, titleSize, ui::theme.text);
        const std::string sub = (showYear && a.year > 0)
                                    ? TextFormat("%s · %d", a.artist.c_str(), a.year)
                                    : a.artist;
        ui::TextEllipsis(sub, Vector2{x, y + artH + 28}, cardW, subSize, ui::theme.textSecondary);
    }
    EndScissorMode();
}

void App::DrawSearchView(Rectangle r) {
    const float pad = 24;

    // ── Search box: leading magnifier + always-focused single-line input ──
    const Rectangle boxRow{r.x + pad, r.y + 24, r.width - pad * 2, 44};
    ui::IconSearch(Vector2{boxRow.x + 11, boxRow.y + boxRow.height / 2}, 18, ui::theme.textTertiary);
    const Rectangle input{boxRow.x + 34, boxRow.y, boxRow.width - 34, boxRow.height};
    const int res = ui::TextInput(input, &searchQuery_, 16);
    if (searchQuery_.empty()) {
        ui::Text("Search tracks, albums, and artists…", Vector2{input.x + 14, input.y + 14}, 15,
                 ui::theme.textTertiary);
    }
    if (res == -1) {  // Esc clears the query, then backs out to the Library.
        if (searchQuery_.empty()) {
            view_ = View::Library;
        } else {
            searchQuery_.clear();
        }
        return;
    }

    // Trimmed, lower-cased query for case-insensitive substring matching.
    std::string q = Lower(searchQuery_);
    while (!q.empty() && q.front() == ' ') q.erase(q.begin());
    while (!q.empty() && q.back() == ' ') q.pop_back();

    if (q.empty()) {
        ui::TextCentered("Start typing to search", Vector2{r.x + r.width / 2, r.y + r.height / 2}, 15,
                         ui::theme.textTertiary);
        return;
    }

    // Re-filter only when the trimmed query or the library contents change.
    // Otherwise reuse the cached result vectors instead of lower-casing and
    // substring-scanning every track/album string every frame.
    if (!searchCacheValid_ || searchCacheKey_ != q || searchCacheGen_ != libGeneration_) {
        const auto contains = [&](const std::string& s) {
            return Lower(s).find(q) != std::string::npos;
        };
        searchAlbums_.clear();
        for (const auto& a : library_.Albums()) {
            if (contains(a.title) || contains(a.artist)) searchAlbums_.push_back(&a);
        }
        searchTracks_.clear();
        for (const auto& t : library_.Tracks()) {
            const Album* a = library_.AlbumById(t.albumId);
            if (contains(t.title) || contains(t.artist) || (a != nullptr && contains(a->artist))) {
                searchTracks_.push_back(&t);
            }
        }
        searchCacheKey_ = q;
        searchCacheGen_ = libGeneration_;
        searchCacheValid_ = true;
    }
    const std::vector<const Album*>& albums = searchAlbums_;
    const std::vector<const Track*>& tracks = searchTracks_;

    if (albums.empty() && tracks.empty()) {
        ui::TextCentered(TextFormat("No results for \"%s\"", searchQuery_.c_str()),
                         Vector2{r.x + r.width / 2, r.y + r.height / 2}, 15, ui::theme.textTertiary);
        return;
    }

    float y = boxRow.y + boxRow.height + 24;
    const float bottom = r.y + r.height;

    // ── Albums: capped at ~40% of the remaining height, own scroll ──
    if (!albums.empty()) {
        ui::Text("ALBUMS", Vector2{r.x + pad, y}, 12, ui::theme.textTertiary);
        y += 24;
        const float cardW = 150, artH = 150, gap = 18;
        const float cardH = artH + 50;
        const Rectangle grid{r.x, y, r.width, std::min((bottom - y) * 0.4f, cardH + gap + 8)};
        DrawAlbumGrid(grid, albums, cardW, artH, gap, 14, 12, /*showYear=*/false,
                      &searchAlbumsScroll_);
        y = grid.y + grid.height + 20;
    }

    // ── Tracks: fill the rest with the shared track table ──
    if (!tracks.empty() && y < bottom - 60) {
        ui::Text("TRACKS", Vector2{r.x + pad, y}, 12, ui::theme.textTertiary);
        y += 22;
        const Rectangle table{r.x, y, r.width, bottom - y};
        const TableResult tr = DrawTrackTable(table, tracks, &searchTracksScroll_, true);
        if (tr.clicked >= 0) {
            // Play the clicked track within the full library — like the Library view —
            // rather than turning the whole search result set into the queue.
            const Track* picked = tracks[tr.clicked];
            std::vector<const Track*> all;
            all.reserve(library_.Tracks().size());
            int pickedIdx = 0;
            for (const auto& t : library_.Tracks()) {
                if (&t == picked) pickedIdx = static_cast<int>(all.size());
                all.push_back(&t);
            }
            PlayFromTrackList(all, pickedIdx);
        }
        if (tr.rightClicked >= 0) OpenTrackMenu(tracks[tr.rightClicked]->id);
    }
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

    if (ShuffleAllButton(r)) ShufflePlay(tracks);

    const Rectangle table{r.x, r.y + 72, r.width, r.height - 72};
    const TableResult res = DrawTrackTable(table, tracks, &libScroll_, true);
    if (res.clicked >= 0) PlayFromTrackList(tracks, res.clicked);
    if (res.rightClicked >= 0) OpenTrackMenu(tracks[res.rightClicked]->id);
}

void App::DrawAlbumsView(Rectangle r) {
    if (library_.Albums().empty()) {
        DrawEmptyState(r);
        return;
    }
    ui::Text("Albums", Vector2{r.x + 24, r.y + 24}, 28, ui::theme.text);

    if (ShuffleAllButton(r)) {
        std::vector<const Track*> all;
        all.reserve(library_.Tracks().size());
        for (const auto& t : library_.Tracks()) all.push_back(&t);
        ShufflePlay(all);
    }

    const float cardW = 172, artH = 172, gap = 20;
    const Rectangle grid{r.x, r.y + 72, r.width, r.height - 72};
    std::vector<const Album*> albums;
    albums.reserve(library_.Albums().size());
    for (const auto& a : library_.Albums()) albums.push_back(&a);
    DrawAlbumGrid(grid, albums, cardW, artH, gap, 15, 13, /*showYear=*/true, &albumsScroll_);
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

    const Rectangle playR{tx, hy + 108, 44 + ui::Measure("Play", 16).x + 24, 38};
    DrawRectangleRounded(playR, 0.6f, 8,
                         ui::Hover(playR) ? Brighten(ui::theme.accent, 0.15f) : ui::theme.accent);
    ui::IconPlay(Vector2{playR.x + 28, playR.y + 19}, 13, ui::theme.bg);
    ui::Text("Play", Vector2{playR.x + 44, playR.y + 10}, 16, ui::theme.bg);
    if (ui::Clicked(playR) && !tracks.empty()) {
        PlayFromTrackList(tracks, 0, QueueSource::Album, album->id);
    }
    float sx = playR.x + playR.width + 12;
    if (ShuffleButton(&sx, hy + 112)) ShufflePlay(tracks, QueueSource::Album, album->id);

    const Rectangle table{r.x, hy + 176, r.width, r.height - (hy + 176 - r.y)};
    const TableResult res = DrawTrackTable(table, tracks, &detailScroll_, false);
    if (res.clicked >= 0) PlayFromTrackList(tracks, res.clicked, QueueSource::Album, album->id);
    if (res.rightClicked >= 0) OpenTrackMenu(tracks[res.rightClicked]->id);
}

void App::DrawPlaylistsView(Rectangle r) {
    ui::Text("Playlists", Vector2{r.x + 24, r.y + 24}, 28, ui::theme.text);
    ui::Text("Drop a .m3u file anywhere to import", Vector2{r.x + 24, r.y + 62}, 13,
             ui::theme.textTertiary);

    const Rectangle newR{r.x + r.width - 24 - 150, r.y + 28, 150, 36};
    DrawRectangleRounded(newR, 0.5f, 8, ui::Hover(newR) ? ui::theme.hover : ui::theme.elevated);
    DrawRectangleRoundedLinesEx(newR, 0.5f, 8, 1, ui::theme.border);
    ui::IconPlus(Vector2{newR.x + 24, newR.y + 18}, 13, ui::theme.text);
    ui::Text("New Playlist", Vector2{newR.x + 40, newR.y + 9}, 14, ui::theme.text);
    if (ui::Clicked(newR)) {
        const std::string id = playlists_.Create("New Playlist").id;
        detailPlaylistId_ = id;
        plDetailScroll_ = 0;
        deleteArmId_.clear();
        editPlaylistId_ = id;  // name it right away
        editText_.clear();
        view_ = View::PlaylistDetail;
        return;
    }

    const float rowH = 64, stride = rowH + 6;
    const auto& lists = playlists_.All();
    const Rectangle listArea{r.x, r.y + 96, r.width, r.height - 96};
    ui::ScrollArea(listArea, static_cast<float>(lists.size()) * stride + 8, &playlistsScroll_);
    BeginScissorMode(static_cast<int>(listArea.x), static_cast<int>(listArea.y),
                     static_cast<int>(listArea.width), static_cast<int>(listArea.height));
    for (size_t i = 0; i < lists.size(); i++) {
        const Playlist& p = lists[i];
        const float y = listArea.y + 8 + static_cast<float>(i) * stride - playlistsScroll_;
        if (y + rowH < listArea.y || y > listArea.y + listArea.height) continue;
        const Rectangle row{r.x + 16, y, r.width - 32, rowH};
        if (ui::Hover(row) && ui::Hover(listArea)) {
            DrawRectangleRounded(row, 0.15f, 6, Fade(ui::theme.hover, 0.6f));
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                detailPlaylistId_ = p.id;
                plDetailScroll_ = 0;
                deleteArmId_.clear();
                view_ = View::PlaylistDetail;
            }
        }
        DrawPlaylistIcon(Rectangle{row.x + 10, y + 10, 44, 44}, p, 1.0f);
        ui::TextEllipsis(p.name, Vector2{row.x + 70, y + 12}, row.width - 180, 16, ui::theme.text);
        ui::Text(TextFormat("%d tracks", static_cast<int>(p.trackIds.size())),
                 Vector2{row.x + 70, y + 36}, 13, ui::theme.textSecondary);
    }
    EndScissorMode();
}

void App::DrawPlaylistDetailView(Rectangle r) {
    const Playlist* p = playlists_.ById(detailPlaylistId_);
    if (p == nullptr) {
        view_ = View::Playlists;
        return;
    }
    const bool isUser = !Playlists::IsSystem(p->id);
    const bool isNowPlaying = p->id == Playlists::kNowPlayingId;
    const auto tracks = ResolveTracks(p->trackIds);
    const QueueSource src = isNowPlaying ? QueueSource::NowPlaying : QueueSource::Playlist;

    const Rectangle backR{r.x + 24, r.y + 18, 92, 24};
    ui::Text("< Playlists", Vector2{backR.x, backR.y + 2}, 14,
             ui::Hover(backR) ? ui::theme.text : ui::theme.textSecondary);
    if (ui::Clicked(backR)) {
        view_ = View::Playlists;
        return;
    }

    const float hy = r.y + 56;
    DrawPlaylistIcon(Rectangle{r.x + 24, hy, 160, 160}, *p, 1.0f);
    const float tx = r.x + 24 + 160 + 24;
    ui::Text("PLAYLIST", Vector2{tx, hy + 8}, 12, ui::theme.textTertiary);
    if (editPlaylistId_ == p->id) {
        const Rectangle inR{tx, hy + 26, std::min(420.0f, r.width - tx - 24), 40};
        const int res = ui::TextInput(inR, &editText_, 18);
        ui::Text("Enter to save · Esc to cancel", Vector2{tx, hy + 74}, 12, ui::theme.textTertiary);
        if (res != 0) {
            if (res == 1) playlists_.Rename(p->id, editText_);
            editPlaylistId_.clear();
        }
    } else {
        ui::TextEllipsis(p->name, Vector2{tx, hy + 28}, r.width - tx - 24, 30, ui::theme.text);
        float dur = 0;
        for (const Track* t : tracks) dur += t->duration;
        ui::TextEllipsis(TextFormat("%d tracks, %s", static_cast<int>(tracks.size()),
                                    ui::FormatTime(dur).c_str()),
                         Vector2{tx, hy + 70}, r.width - tx - 24, 14, ui::theme.textSecondary);
    }

    float bx = tx;
    const Rectangle playR{bx, hy + 108, 44 + ui::Measure("Play", 16).x + 24, 38};
    DrawRectangleRounded(playR, 0.6f, 8,
                         ui::Hover(playR) ? Brighten(ui::theme.accent, 0.15f) : ui::theme.accent);
    ui::IconPlay(Vector2{playR.x + 28, playR.y + 19}, 13, ui::theme.bg);
    ui::Text("Play", Vector2{playR.x + 44, playR.y + 10}, 16, ui::theme.bg);
    if (ui::Clicked(playR) && !tracks.empty()) PlayFromTrackList(tracks, 0, src, p->id);
    bx += playR.width + 12;
    if (ShuffleButton(&bx, hy + 112)) ShufflePlay(tracks, src, p->id);

    const auto button = [&](const std::string& label, float w) {
        const Rectangle b{bx, hy + 112, w, 30};
        if (ui::Hover(b)) DrawRectangleRounded(b, 0.6f, 8, Fade(ui::theme.hover, 0.7f));
        DrawRectangleRoundedLinesEx(b, 0.6f, 8, 1, ui::theme.border);
        ui::TextCentered(label, Vector2{b.x + w / 2, b.y + 15}, 13,
                         ui::Hover(b) ? ui::theme.text : ui::theme.textSecondary);
        bx += w + 10;
        return ui::Clicked(b);
    };
    if (button("Export .m3u8", 104) && !tracks.empty()) ExportPlaylist(*p);
    if (isUser) {
        if (button("Rename", 76)) {
            editPlaylistId_ = p->id;
            editText_ = p->name;
        }
        const bool armed = deleteArmId_ == p->id;
        if (button(armed ? "Confirm delete" : "Delete", armed ? 116 : 76)) {
            if (armed) {
                playlists_.Remove(p->id);
                view_ = View::Playlists;
                return;
            }
            deleteArmId_ = p->id;
        }
    } else if (isNowPlaying) {
        if (button("Clear", 64) && !p->trackIds.empty()) {
            playlists_.NowPlaying().trackIds.clear();
            playlists_.Save();
            if (player_.Source() == QueueSource::NowPlaying) player_.ClearQueue();
            return;
        }
    }

    const Rectangle table{r.x, hy + 176, r.width, r.height - (hy + 176 - r.y)};
    if (tracks.empty()) {
        ui::TextCentered("No tracks yet — right-click any track to add it here",
                         Vector2{r.x + r.width / 2, table.y + 80}, 14, ui::theme.textSecondary);
        return;
    }
    const TableResult res = DrawTrackTable(table, tracks, &plDetailScroll_, true, true);
    if (res.clicked >= 0) PlayFromTrackList(tracks, res.clicked, src, p->id);
    if (res.rightClicked >= 0) OpenTrackMenu(tracks[res.rightClicked]->id);
    if (res.removed >= 0) {
        const std::string tid = tracks[res.removed]->id;
        playlists_.RemoveTrack(p->id, tid);
        if (isNowPlaying && player_.Source() == QueueSource::NowPlaying) {
            player_.RemoveTrackId(tid);
        }
    }
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

    // Ambient wash from the album's dominant color, breathing with the bass
    const float bass = visualizer_.BassLevel();
    DrawRectangleGradientV(static_cast<int>(r.x), static_cast<int>(r.y),
                           static_cast<int>(r.width), static_cast<int>(r.height * 0.75f),
                           Fade(glow, 0.10f + 0.10f * bass), Fade(glow, 0.0f));

    // Art sits above the track text and the inline transport; reserve room at
    // the bottom for that control cluster so everything stacks like the web app.
    // Let the cover grow larger in fullscreen so the immersive view fills the
    // extra space rather than floating a small square in the middle.
    const float capFg = fullscreen_ ? 620.0f : 440.0f;
    const float capNoFg = fullscreen_ ? 520.0f : 360.0f;
    const float artSize = hasFg ? std::min({capFg, r.height - 290, r.width - 200})
                                : std::min({capNoFg, r.height - 290, r.width - 160});
    const float artX = r.x + (r.width - artSize) / 2;
    // Without depth layers the bars fill the bottom, so the art stays up top.
    // With them the visualizer lives inside the art and there are no bottom
    // bars to balance against, so center the art + panel block on the screen.
    // Block spans the art down to the transport row (textY + 142 below the art).
    const float blockH = artSize + 164;
    const float artY = hasFg ? r.y + std::max(40.0f, (r.height - blockH) / 2) : r.y + 48;
    const Rectangle artRect{artX, artY, artSize, artSize};

    // Ambient glow: an Ambilight-style bloom whose color is the cover's own
    // edges (blurred in AlbumGlow), drawn enlarged + additive so light appears
    // to spill from the art. Breathes a little wider with the bass.
    const float glowBase = 0.16f + 0.20f * bass;
    if (albumGlow_.Ready()) {
        const float full = artSize / AlbumGlow::kContentFrac;  // content maps to the art
        const float margin = (full - artSize) / 2 + 6.0f * bass;
        const Rectangle gdst{artX - margin, artY - margin, artSize + 2 * margin,
                             artSize + 2 * margin};
        const Texture2D& gt = albumGlow_.Texture();
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(gt, Rectangle{0, 0, static_cast<float>(gt.width),
                                     -static_cast<float>(gt.height)},
                       gdst, Vector2{0, 0}, 0, Fade(WHITE, 0.55f + 1.4f * glowBase));
        EndBlendMode();
    } else {
        // Bloom not built yet (cover still decoding): keep a flat dominant halo.
        DrawRectangleRounded(Rectangle{artX - 10, artY - 10, artSize + 20, artSize + 20}, 0.06f, 8,
                             Fade(glow, glowBase * 0.5f));
    }

    if (config_.vinylDisc && shownAlbum != nullptr) {
        vinyl_.Draw(artRect, shownAlbum->accent, shownAlbum->dominant);
    }

    // Album switch: the sleeve turns over as a real 3D slab (front cover ->
    // patina edges -> incoming cover on the back). The slab is rendered offscreen
    // in Frame(); here it cross-fades over the flat cover at the turn's ends so
    // the 2D->3D mode change doesn't pop, and owns the middle of the turn.
    const float flip = vinyl_.FlipProgress();
    if (vinyl_.Flipping() && flip3dReady_) {
        const auto smooth = [](float x) {
            x = std::clamp(x, 0.0f, 1.0f);
            return x * x * (3.0f - 2.0f * x);
        };
        const float slabA = std::min(smooth(flip / 0.12f), smooth((1.0f - flip) / 0.12f));
        if (slabA < 1.0f) DrawAlbumArt(artRect, shownAlbum, 1.4f, 1.0f - slabA);
        const Texture2D& slab = sleeve3d_.Texture();
        const Rectangle src{0, 0, static_cast<float>(slab.width),
                            -static_cast<float>(slab.height)};  // render textures are y-flipped
        // The cover fills only kCoverFrac of the target; scale up so it maps to
        // the art rect and the turning corners overshoot the art, not the frame.
        const float inflate = artSize * (1.0f / Sleeve3D::kCoverFrac - 1.0f) / 2.0f;
        const Rectangle sdst{artRect.x - inflate, artRect.y - inflate, artSize + 2 * inflate,
                             artSize + 2 * inflate};
        DrawTexturePro(slab, src, sdst, Vector2{0, 0}, 0, Fade(WHITE, slabA));
    } else if (vinyl_.Flipping()) {
        // Both covers not yet decoded: flat squish keeps the turn going.
        const float w = std::fabs(std::cos(flip * PI));
        DrawAlbumArt(Rectangle{artRect.x + artRect.width * (1 - w) / 2, artRect.y,
                               artRect.width * w, artRect.height},
                     shownAlbum, 1.4f, 1.0f);
    } else {
        DrawAlbumArt(artRect, shownAlbum, 1.4f, 1.0f);
    }

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

    // Everything below the art lives in a centered column, narrow enough that
    // the header buttons and transport read as one panel rather than spanning
    // the whole window.
    const float cx = r.x + r.width / 2;
    const float colW = std::min(560.0f, r.width - 96);
    const float colX = cx - colW / 2;

    const float textY = artY + artSize + 28;
    // The title shares its row with the action buttons hugging the column's
    // right edge (~92px), so keep the marquee window clear of them — mirrored on
    // the left to stay centred. Long titles scroll instead of overflowing.
    ui::TextMarqueeCentered(cur->title, Vector2{cx, textY}, colW - 200, 26,
                            ui::theme.text);
    std::string sub = cur->artist;
    if (album != nullptr) sub += "  \xc2\xb7  " + album->title;
    ui::TextMarqueeCentered(sub, Vector2{cx, textY + 30}, colW, 15,
                            ui::theme.textSecondary);

    // Paint-mask button: opens the brush editor for the album on screen, so the
    // user can hand-correct (or hand-draw) the depth mask behind the visualizer.
    if (shownAlbum != nullptr && !shownAlbum->artPath.empty()) {
        const Rectangle brushR{colX + colW - 92, textY - 12, 24, 24};
        ui::IconBrush(Vector2{brushR.x + 12, brushR.y + 12}, 17,
                      ui::Hover(brushR) ? ui::theme.text : ui::theme.textSecondary);
        if (ui::Clicked(brushR)) OpenBrushEditor(*shownAlbum);
    }

    // Favorite + add-to-playlist, level with the title at the column's right.
    const Rectangle heartR{colX + colW - 58, textY - 12, 24, 24};
    const bool fav = playlists_.IsFavorite(cur->id);
    ui::IconHeart(Vector2{heartR.x + 12, heartR.y + 12}, 18,
                  fav            ? ui::theme.accent
                  : ui::Hover(heartR) ? ui::theme.text
                                      : ui::theme.textSecondary,
                  fav);
    if (ui::Clicked(heartR)) playlists_.ToggleFavorite(cur->id);

    const Rectangle plusR{colX + colW - 24, textY - 12, 24, 24};
    ui::IconPlus(Vector2{plusR.x + 12, plusR.y + 12}, 18,
                 ui::Hover(plusR) ? ui::theme.text : ui::theme.textSecondary);
    if (ui::Clicked(plusR)) OpenTrackMenu(cur->id);

    // Seek bar and transport sit directly beneath the track text so the art,
    // title, buttons and controls all read as one panel under the album, like
    // the web app — rather than the transport floating at the window's edge.
    const float seekTop = textY + 64;
    const float ctrlY = seekTop + 50;

    // Visualizer bars rise from the bottom of the view, underneath the controls;
    // they fill the leftover space without dictating where the panel sits.
    if (!hasFg) {
        const float barBottom = r.y + r.height - 10;
        const float barH = std::min(220.0f, barBottom - (ctrlY + 30));
        if (barH > 8) {
            visualizer_.DrawBars(Rectangle{colX, barBottom - barH, colW, barH},
                                 album != nullptr ? album->accent : Brighten(glow, 0.25f));
        }
        if (config_.depthLayers && depth_.Busy()) {
            ui::TextCentered(TextFormat("preparing depth layers (%s)...", depth_.StatusText()),
                             Vector2{cx, r.y + r.height - 22}, 13, ui::theme.textTertiary);
        }
    }

    const float length = player_.TimeLength();
    const float played = player_.TimePlayed();
    if (!seekDragging_) seekValue_ = length > 0 ? played / length : 0;
    const Rectangle seekR{colX, seekTop, colW, 4};
    const bool wasDragging = seekDragging_;
    BarSlider(seekR, &seekValue_, &seekDragging_, ui::theme.accent);
    if (wasDragging && !seekDragging_) player_.SeekTo(seekValue_ * length);
    const float shownTime = seekDragging_ ? seekValue_ * length : played;
    ui::Text(ui::FormatTime(shownTime), Vector2{seekR.x, seekTop + 12}, 12, ui::theme.textSecondary);
    ui::TextRight(ui::FormatTime(length), Vector2{seekR.x + seekR.width, seekTop + 12}, 12,
                  ui::theme.textSecondary);

    const auto iconButton = [&](float x, float halfSize) {
        return Rectangle{x - halfSize, ctrlY - halfSize, halfSize * 2, halfSize * 2};
    };

    const Rectangle shuffleR = iconButton(cx - 110, 14);
    const Color shuffleCol = player_.Shuffle() ? ui::theme.accent
                             : ui::Hover(shuffleR) ? ui::theme.text
                                                   : ui::theme.textSecondary;
    ui::IconShuffle(Vector2{cx - 110, ctrlY}, 16, shuffleCol);
    if (ui::Clicked(shuffleR)) player_.ToggleShuffle();

    const Rectangle prevR = iconButton(cx - 60, 14);
    ui::IconPrev(Vector2{cx - 60, ctrlY}, 18,
                 ui::Hover(prevR) ? ui::theme.text : ui::theme.textSecondary);
    if (ui::Clicked(prevR)) {
        player_.Prev();
        manualSkip_ = true;
    }

    const Rectangle playR = iconButton(cx, 22);
    DrawCircleV(Vector2{cx, ctrlY}, 22,
                ui::Hover(playR) ? Brighten(ui::theme.accent, 0.15f) : ui::theme.accent);
    if (player_.IsPlaying()) {
        ui::IconPause(Vector2{cx, ctrlY}, 16, ui::theme.bg);
    } else {
        ui::IconPlay(Vector2{cx + 1, ctrlY}, 18, ui::theme.bg);
    }
    if (ui::Clicked(playR)) player_.TogglePause();

    const Rectangle nextR = iconButton(cx + 60, 14);
    ui::IconNext(Vector2{cx + 60, ctrlY}, 18,
                 ui::Hover(nextR) ? ui::theme.text : ui::theme.textSecondary);
    if (ui::Clicked(nextR)) {
        player_.Next();
        manualSkip_ = true;
    }

    const Rectangle repeatR = iconButton(cx + 110, 14);
    const bool repeatOn = player_.Repeat() != RepeatMode::Off;
    const Color repeatCol = repeatOn ? ui::theme.accent
                            : ui::Hover(repeatR) ? ui::theme.text
                                                 : ui::theme.textSecondary;
    ui::IconRepeat(Vector2{cx + 110, ctrlY}, 15, repeatCol, player_.Repeat() == RepeatMode::One);
    if (ui::Clicked(repeatR)) player_.CycleRepeat();

    // Volume on the column's left edge, queue toggle on its right.
    float vol = player_.Volume();
    const Vector2 volIcon{colX + 9, ctrlY};
    ui::IconVolume(volIcon, 17, ui::theme.textSecondary, vol);
    const Rectangle volR{colX + 32, ctrlY - 2, 72, 4};
    if (BarSlider(volR, &vol, &volumeDragging_, ui::theme.text)) player_.SetVolume(vol);
    const Rectangle volIconR{volIcon.x - 12, volIcon.y - 12, 24, 24};
    if (ui::Clicked(volIconR)) player_.SetVolume(vol > 0.01f ? 0.0f : 0.8f);

    const Vector2 queueIcon{colX + colW - 12, ctrlY};
    const Rectangle queueR{queueIcon.x - 12, queueIcon.y - 12, 24, 24};
    const Color queueCol = config_.queuePanel ? ui::theme.accent
                           : ui::Hover(queueR) ? ui::theme.text
                                               : ui::theme.textSecondary;
    ui::IconQueue(queueIcon, 16, queueCol);
    if (ui::Clicked(queueR)) ToggleQueuePanel();
}

void App::DrawSettingsView(Rectangle r) {
    ui::Text("Settings", Vector2{r.x + 24, r.y + 24}, 28, ui::theme.text);

    const float pad = 20;
    const Rectangle card{r.x + 24, r.y + 84, std::min(r.width - 48, 560.0f), 150};
    DrawRectangleRounded(card, 0.1f, 8, ui::theme.surface);
    DrawRectangleRoundedLinesEx(card, 0.1f, 8, 1, ui::theme.borderSubtle);

    ui::Text("Appearance", Vector2{card.x + pad, card.y + 18}, 13, ui::theme.textSecondary);
    ui::Text("Theme", Vector2{card.x + pad, card.y + 42}, 17, ui::theme.text);

    // Segmented control: System / Light / Dark. Selecting one applies the
    // palette immediately and persists the preference.
    struct Seg {
        const char* label;
        const char* value;
    };
    const Seg segs[] = {{"System", "system"}, {"Light", "light"}, {"Dark", "dark"}};
    const float segW = 100, segH = 34, gap = 8;
    float sx = card.x + pad;
    const float sy = card.y + 78;
    for (const auto& seg : segs) {
        const Rectangle b{sx, sy, segW, segH};
        const bool sel = config_.theme == seg.value;
        if (sel) {
            DrawRectangleRounded(b, 0.35f, 6, Fade(ui::theme.accent, 0.15f));
            DrawRectangleRoundedLinesEx(b, 0.35f, 6, 1, ui::theme.accent);
        } else if (ui::Hover(b)) {
            DrawRectangleRounded(b, 0.35f, 6, ui::theme.elevated);
        }
        ui::TextCentered(seg.label, Vector2{b.x + b.width / 2, b.y + b.height / 2}, 15,
                         sel ? ui::theme.accent : ui::theme.textSecondary);
        if (ui::Clicked(b) && !sel) {
            config_.theme = seg.value;
            ApplyTheme();
            config_.Save();
        }
        sx += segW + gap;
    }

    const char* hint = config_.theme == "system"
                           ? "Follows your desktop's color scheme."
                           : "Forcing a fixed palette, ignoring the desktop setting.";
    ui::Text(hint, Vector2{card.x + pad, sy + segH + 12}, 13, ui::theme.textTertiary);

    DrawFolderSettings(Rectangle{card.x, card.y + card.height + 20, card.width, 0});
}

void App::DrawFolderSettings(Rectangle anchor) {
    const float pad = 20;
    const auto& folders = library_.Folders();
    const int nf = static_cast<int>(folders.size());

    const float listTop = 74;
    const float rowH = 32;
    const float inputY = listTop + (nf > 0 ? nf * rowH : rowH) + 6;
    const float inputH = 38;
    const float hintY = inputY + inputH + 14;
    const Rectangle card{anchor.x, anchor.y, anchor.width, hintY + 18 + pad};
    DrawRectangleRounded(card, 0.08f, 8, ui::theme.surface);
    DrawRectangleRoundedLinesEx(card, 0.08f, 8, 1, ui::theme.borderSubtle);

    ui::Text("Library", Vector2{card.x + pad, card.y + 18}, 13, ui::theme.textSecondary);
    ui::Text("Music Folders", Vector2{card.x + pad, card.y + 42}, 17, ui::theme.text);

    // ── Existing folders, each with a remove button ──
    std::string removeFolder;
    if (nf == 0) {
        ui::Text("No folders yet — add a path or drop music onto the window.",
                 Vector2{card.x + pad, card.y + listTop + 4}, 13, ui::theme.textTertiary);
    }
    for (int i = 0; i < nf; i++) {
        const Rectangle row{card.x + pad, card.y + listTop + i * rowH, card.width - 2 * pad,
                            rowH - 6};
        if (ui::Hover(row)) DrawRectangleRounded(row, 0.35f, 6, Fade(ui::theme.elevated, 0.6f));
        const float cy = row.y + row.height / 2;
        ui::TextEllipsis(folders[i], Vector2{row.x + 8, cy - 7}, row.width - 44, 14, ui::theme.text);
        const Rectangle rm{row.x + row.width - 28, cy - 11, 22, 22};
        const bool rmHover = ui::Hover(rm);
        if (rmHover) DrawCircleV(Vector2{rm.x + 11, rm.y + 11}, 11, ui::theme.hover);
        ui::IconClose(Vector2{rm.x + 11, rm.y + 11}, 10,
                      rmHover ? ui::theme.text : ui::theme.textSecondary);
        if (ui::Clicked(rm)) removeFolder = folders[i];
    }

    // ── Add-by-path field + button ──
    const Rectangle inputRow{card.x + pad, card.y + inputY, card.width - 2 * pad - 86, inputH};
    const Rectangle addBtn{inputRow.x + inputRow.width + 10, card.y + inputY, 76, inputH};

    const auto commitAdd = [&]() {
        std::string path = folderInput_;
        while (!path.empty() && (path.front() == ' ' || path.front() == '\t')) path.erase(path.begin());
        while (!path.empty() && (path.back() == ' ' || path.back() == '\t' || path.back() == '\n'))
            path.pop_back();
        if (path.empty()) return;
        // Expand a leading ~ only for "~" or a "~/" prefix; leave "~user" forms untouched.
        if (path == "~" || (path.size() >= 2 && path[0] == '~' && path[1] == '/')) {
            path = paths::Home() + path.substr(1);
        }
        if (DirectoryExists(path.c_str())) {
            library_.AddFolder(path);
            SyncWatcher();
            Toast("Added folder");
            folderInput_.clear();
            folderInputActive_ = false;
        } else {
            Toast("Folder not found");
        }
    };

    if (folderInputActive_) {
        if (const int res = ui::TextInput(inputRow, &folderInput_, 15); res == 1) {
            commitAdd();
        } else if (res == -1) {
            folderInputActive_ = false;
        }
    } else {
        DrawRectangleRounded(inputRow, 0.25f, 6, ui::theme.elevated);
        DrawRectangleRoundedLinesEx(inputRow, 0.25f, 6, 1, ui::theme.border);
        ui::TextEllipsis(folderInput_.empty() ? "/path/to/folder…" : folderInput_,
                         Vector2{inputRow.x + 12, inputRow.y + inputRow.height / 2 - 8},
                         inputRow.width - 24, 15,
                         folderInput_.empty() ? ui::theme.textTertiary : ui::theme.text);
        if (ui::Clicked(inputRow)) {
            folderInputActive_ = true;
            MarkActivity();
        }
    }

    const bool addHover = ui::Hover(addBtn);
    DrawRectangleRounded(addBtn, 0.3f, 6, addHover ? Brighten(ui::theme.accent, 0.12f) : ui::theme.accent);
    ui::TextCentered("Add", Vector2{addBtn.x + addBtn.width / 2, addBtn.y + addBtn.height / 2}, 15,
                     ui::theme.bg);
    if (ui::Clicked(addBtn)) commitAdd();

    // Click anywhere outside the field (and not on Add) drops focus.
    if (folderInputActive_ && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !ui::Hover(inputRow) &&
        !ui::Hover(addBtn)) {
        folderInputActive_ = false;
    }

    ui::Text("Or drop a folder anywhere in the window.",
             Vector2{card.x + pad, card.y + hintY}, 13, ui::theme.textTertiary);

    if (!removeFolder.empty()) {
        library_.RemoveFolder(removeFolder);
        // Removing the last folder clears the track/album vectors directly
        // (no PollScan), so the cached search results would dangle.
        libGeneration_++;
        mosaic_.Rebuild(library_.Albums(), MosaicCfg());
        backdrop_.MarkDirty();
        SyncWatcher();
        Toast("Removed folder");
        folderInputActive_ = false;
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

void App::DrawPlaylistIcon(Rectangle r, const Playlist& p, float iconScale) {
    DrawRectangleRounded(r, 0.12f, 6, ui::theme.elevated);
    const Vector2 c{r.x + r.width / 2, r.y + r.height / 2};
    const float s = std::min(r.width, r.height) * 0.42f * iconScale;
    if (p.id == Playlists::kFavoritesId) {
        ui::IconHeart(c, s, kHeartRed, true);
    } else if (p.id == Playlists::kNowPlayingId) {
        ui::IconQueue(c, s, ui::theme.accent);
    } else {
        ui::IconNote(c, s, ui::theme.textSecondary);
    }
    DrawRectangleLinesEx(r, 1, ui::theme.borderSubtle);
}

void App::ToggleQueuePanel() {
    config_.queuePanel = !config_.queuePanel;
    MarkActivity();
}

void App::DrawQueuePanel(Rectangle r) {
    backdrop_.DrawGlass(r, Fade(ui::theme.surface, 0.72f));
    DrawLineEx(Vector2{r.x, r.y}, Vector2{r.x, r.y + r.height}, 1, ui::theme.borderSubtle);
    // Content is laid out at full panel width anchored to the sliding left
    // edge; whatever exceeds the window is clipped by the screen itself.
    const float x0 = r.x;

    enum class Mode { Queue, NowPlayingList, Empty };
    Mode mode = Mode::Empty;
    std::vector<const Track*> rows;
    std::string title = "Queue";
    const bool nowPlayingSource = player_.Source() == QueueSource::NowPlaying;
    if (player_.QueueSize() > 0) {
        mode = Mode::Queue;
        rows.reserve(player_.QueueSize());
        for (int i = 0; i < player_.QueueSize(); i++) rows.push_back(player_.TrackAtOrderPos(i));
        switch (player_.Source()) {
            case QueueSource::NowPlaying: title = "Now Playing"; break;
            case QueueSource::Playlist: {
                const Playlist* p = playlists_.ById(player_.SourceId());
                title = p != nullptr ? p->name : "Playlist";
                break;
            }
            case QueueSource::Album: {
                const Album* a = library_.AlbumById(player_.SourceId());
                title = a != nullptr ? a->title : "Album";
                break;
            }
            case QueueSource::Library: title = "Library"; break;
            default: break;
        }
    } else if (!playlists_.NowPlaying().trackIds.empty()) {
        // Queue idle but the Now Playing list has tracks parked: show it with
        // a Play button, like the web's QueuePanel fallback.
        mode = Mode::NowPlayingList;
        rows = ResolveTracks(playlists_.NowPlaying().trackIds);
        title = "Now Playing";
    }

    ui::TextEllipsis(title, Vector2{x0 + 20, r.y + 20}, kQueueW - 88, 17, ui::theme.text);
    ui::Text(TextFormat("%d tracks", static_cast<int>(rows.size())), Vector2{x0 + 20, r.y + 46},
             12, ui::theme.textSecondary);
    const Rectangle closeR{x0 + kQueueW - 44, r.y + 16, 28, 28};
    ui::IconClose(Vector2{closeR.x + 14, closeR.y + 14}, 14,
                  ui::Hover(closeR) ? ui::theme.text : ui::theme.textSecondary);
    if (ui::Clicked(closeR)) ToggleQueuePanel();

    float top = r.y + 74;
    const bool showPlay = mode == Mode::NowPlayingList;
    const bool showClear =
        mode == Mode::NowPlayingList || (mode == Mode::Queue && nowPlayingSource);
    if (showPlay || showClear) {
        float bx = x0 + 20;
        if (showPlay) {
            const Rectangle pR{bx, top, 76, 30};
            DrawRectangleRounded(pR, 0.6f, 8,
                                 ui::Hover(pR) ? Brighten(ui::theme.accent, 0.15f)
                                               : ui::theme.accent);
            ui::IconPlay(Vector2{pR.x + 19, pR.y + 15}, 11, ui::theme.bg);
            ui::Text("Play", Vector2{pR.x + 31, pR.y + 7}, 14, ui::theme.bg);
            if (ui::Clicked(pR) && !rows.empty()) {
                PlayFromTrackList(rows, 0, QueueSource::NowPlaying, Playlists::kNowPlayingId);
            }
            bx += 86;
        }
        if (showClear) {
            const Rectangle cR{bx, top, 70, 30};
            if (ui::Hover(cR)) DrawRectangleRounded(cR, 0.6f, 8, Fade(ui::theme.hover, 0.7f));
            DrawRectangleRoundedLinesEx(cR, 0.6f, 8, 1, ui::theme.border);
            ui::TextCentered("Clear", Vector2{cR.x + 35, cR.y + 15}, 13,
                             ui::Hover(cR) ? ui::theme.text : ui::theme.textSecondary);
            if (ui::Clicked(cR)) {
                playlists_.NowPlaying().trackIds.clear();
                playlists_.Save();
                if (nowPlayingSource) player_.ClearQueue();
                return;
            }
        }
        top += 42;
    }

    if (mode == Mode::Empty) {
        ui::TextCentered("Queue is empty", Vector2{x0 + kQueueW / 2, r.y + r.height / 2 - 12}, 15,
                         ui::theme.textSecondary);
        ui::TextCentered("Play something, or right-click tracks",
                         Vector2{x0 + kQueueW / 2, r.y + r.height / 2 + 12}, 12,
                         ui::theme.textTertiary);
        ui::TextCentered("to add them to Now Playing",
                         Vector2{x0 + kQueueW / 2, r.y + r.height / 2 + 30}, 12,
                         ui::theme.textTertiary);
        return;
    }

    const Rectangle list{x0, top, kQueueW, r.y + r.height - top};
    const int n = static_cast<int>(rows.size());
    const Track* current = player_.Current();

    // Follow the currently playing track: when it advances, scroll to keep it
    // centered. We key off the playing track's identity (not its row index), so
    // manual scrolling between advances is left untouched and removing a row
    // above the playing track doesn't yank the view to recenter.
    int curRow = -1;
    std::string curId;
    if (mode == Mode::Queue) {
        curRow = player_.OrderPos();
        const Track* cur = player_.TrackAtOrderPos(curRow);
        if (cur != nullptr) curId = cur->id;
    } else if (current != nullptr) {
        for (int i = 0; i < n; i++) {
            if (rows[i] != nullptr && rows[i]->id == current->id) {
                curRow = i;
                curId = current->id;
                break;
            }
        }
    }
    if (curRow >= 0 && curId != queueFollowId_) {
        queueScroll_ = curRow * kQueueRowH - (list.height - kQueueRowH) / 2.0f;
    }
    queueFollowId_ = curId;

    ui::ScrollArea(list, static_cast<float>(rows.size()) * kQueueRowH, &queueScroll_);
    const int first = std::max(0, static_cast<int>(queueScroll_ / kQueueRowH));
    const int last = std::min(n, static_cast<int>((queueScroll_ + list.height) / kQueueRowH) + 1);

    int jump = -1, removeIdx = -1;
    BeginScissorMode(static_cast<int>(list.x), static_cast<int>(list.y),
                     static_cast<int>(list.width), static_cast<int>(list.height));
    for (int i = first; i < last; i++) {
        const Track* t = rows[i];
        const float y = list.y + i * kQueueRowH - queueScroll_;
        const Rectangle row{x0 + 8, y, kQueueW - 16, kQueueRowH};
        const bool isCurrent = mode == Mode::Queue
                                   ? i == player_.OrderPos()
                                   : (current != nullptr && t != nullptr && current->id == t->id);
        const bool rowHovered = ui::Hover(row) && ui::Hover(list);
        if (rowHovered) {
            DrawRectangleRounded(row, 0.2f, 6, Fade(ui::theme.hover, 0.6f));
            // The remove button replaces the duration while hovered
            const Rectangle xR{row.x + row.width - 30, y + kQueueRowH / 2 - 10, 20, 20};
            const bool overRemove = ui::Hover(xR);
            ui::IconClose(Vector2{xR.x + 10, xR.y + 10}, 13,
                          overRemove ? ui::theme.text : ui::theme.textTertiary);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (overRemove) removeIdx = i;
                else jump = i;
            }
        }
        constexpr float kArt = 40.0f;
        const Rectangle art{row.x + 8, y + (kQueueRowH - kArt) / 2, kArt, kArt};
        DrawAlbumArt(art, t != nullptr ? library_.AlbumById(t->albumId) : nullptr, 1.0f, 1.0f);
        if (isCurrent) {
            DrawRectangleRounded(art, 0.06f, 6, Fade(BLACK, 0.45f));
            ui::IconNote(Vector2{art.x + kArt / 2, art.y + kArt / 2}, 15, ui::theme.accent);
        }
        const float textX = art.x + kArt + 12;
        const float textW = row.width - (textX - row.x) - 76;
        if (t != nullptr) {
            ui::TextEllipsis(t->title, Vector2{textX, y + 9}, textW, 14,
                             isCurrent ? ui::theme.accent : ui::theme.text);
            ui::TextEllipsis(t->artist, Vector2{textX, y + 29}, textW, 12,
                             ui::theme.textTertiary);
            if (!rowHovered) {
                ui::TextRight(ui::FormatTime(t->duration),
                              Vector2{row.x + row.width - 12, y + 17}, 12,
                              ui::theme.textSecondary);
            }
        } else {
            ui::Text("(missing track)", Vector2{textX, y + 17}, 13, ui::theme.textTertiary);
        }
    }
    EndScissorMode();

    if (jump >= 0) {
        if (mode == Mode::Queue) {
            player_.JumpTo(jump);
            manualSkip_ = true;
            MarkActivity();
        } else {
            PlayFromTrackList(rows, jump, QueueSource::NowPlaying, Playlists::kNowPlayingId);
        }
    }
    if (removeIdx >= 0 && rows[removeIdx] != nullptr) {
        const std::string tid = rows[removeIdx]->id;
        if (mode == Mode::Queue) {
            player_.RemoveAt(removeIdx);
            if (nowPlayingSource) playlists_.RemoveTrack(Playlists::kNowPlayingId, tid);
        } else {
            playlists_.RemoveTrack(Playlists::kNowPlayingId, tid);
        }
        MarkActivity();
    }
}

void App::OpenTrackMenu(const std::string& trackId) {
    menu_.open = true;
    menu_.justOpened = true;
    menu_.trackId = trackId;
    menu_.pos = GetMousePosition();
    MarkActivity();
}

void App::DrawTrackMenu() {
    if (!menu_.open) return;
    const Track* t = library_.TrackById(menu_.trackId);
    if (t == nullptr) {
        menu_.open = false;
        return;
    }

    struct Item {
        std::string label;
        std::function<void()> fn;
        bool sep = false;  // divider above
    };
    std::vector<Item> items;
    items.push_back({"Play", [this, t] {
                         PlayFromTrackList({t}, 0, QueueSource::None);
                     }});
    const bool fav = playlists_.IsFavorite(t->id);
    items.push_back({fav ? "Remove from Favorites" : "Add to Favorites",
                     [this, t] { playlists_.ToggleFavorite(t->id); }});
    const bool inNp = playlists_.Contains(Playlists::kNowPlayingId, t->id);
    items.push_back({inNp ? "Remove from Now Playing" : "Add to Now Playing",
                     [this, t, inNp] {
                         if (inNp) {
                             playlists_.RemoveTrack(Playlists::kNowPlayingId, t->id);
                             if (player_.Source() == QueueSource::NowPlaying) {
                                 player_.RemoveTrackId(t->id);
                             }
                         } else {
                             playlists_.AddTrack(Playlists::kNowPlayingId, t->id);
                             // Live queue built from this list: keep it in sync.
                             if (player_.Source() == QueueSource::NowPlaying) {
                                 player_.Append(t->id);
                             }
                         }
                     }});
    bool sep = true;
    for (const auto& p : playlists_.All()) {
        if (Playlists::IsSystem(p.id)) continue;
        const bool has = playlists_.Contains(p.id, t->id);
        const std::string pid = p.id;
        items.push_back({(has ? "Remove from " : "Add to ") + p.name,
                         [this, t, pid, has] {
                             if (has) playlists_.RemoveTrack(pid, t->id);
                             else playlists_.AddTrack(pid, t->id);
                         },
                         sep});
        sep = false;
    }
    items.push_back({"New playlist with track",
                     [this, t] {
                         const std::string id = playlists_.Create("New Playlist").id;
                         playlists_.AddTrack(id, t->id);
                         detailPlaylistId_ = id;
                         plDetailScroll_ = 0;
                         deleteArmId_.clear();
                         editPlaylistId_ = id;
                         editText_.clear();
                         view_ = View::PlaylistDetail;
                     },
                     sep});

    const float W = static_cast<float>(GetScreenWidth());
    const float H = static_cast<float>(GetScreenHeight());
    const float w = 240, ih = 34, sepH = 9;
    float h = 16;
    for (const auto& it : items) h += ih + (it.sep ? sepH : 0);
    const Vector2 pos{std::min(menu_.pos.x, W - w - 8), std::min(menu_.pos.y, H - h - 8)};
    const Rectangle box{pos.x, pos.y, w, h};
    DrawRectangleRounded(Rectangle{box.x + 3, box.y + 4, w, h}, 0.08f, 6, Fade(BLACK, 0.4f));
    DrawRectangleRounded(box, 0.08f, 6, ui::theme.elevated);
    DrawRectangleLinesEx(box, 1, ui::theme.border);

    bool clickedItem = false;
    float y = box.y + 8;
    for (const auto& it : items) {
        if (it.sep) {
            DrawLineEx(Vector2{box.x + 10, y + 4}, Vector2{box.x + w - 10, y + 4}, 1,
                       ui::theme.borderSubtle);
            y += sepH;
        }
        const Rectangle row{box.x + 6, y, w - 12, ih};
        if (ui::HoverRaw(row)) DrawRectangleRounded(row, 0.25f, 6, ui::theme.hover);
        ui::TextEllipsis(it.label, Vector2{row.x + 10, row.y + 8}, row.width - 20, 14,
                         ui::theme.text);
        if (!menu_.justOpened && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && ui::HoverRaw(row)) {
            it.fn();
            clickedItem = true;
        }
        y += ih;
    }
    if (!menu_.justOpened &&
        (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
        if (clickedItem || !ui::HoverRaw(box)) menu_.open = false;
    }
    menu_.justOpened = false;
}

void App::Toast(const std::string& msg) {
    toast_ = msg;
    toastUntil_ = GetTime() + 3.5;
    MarkActivity();
}

void App::DrawToast(float chromeH) {
    if (GetTime() >= toastUntil_) return;
    const float W = static_cast<float>(GetScreenWidth());
    const float H = static_cast<float>(GetScreenHeight());
    const Vector2 m = ui::Measure(toast_, 14);
    const float w = std::min(m.x + 36, W - 40), h = 40;
    const Rectangle box{(W - w) / 2, H - chromeH - h - 24, w, h};
    DrawRectangleRounded(box, 0.5f, 8, Fade(ui::theme.elevated, 0.97f));
    DrawRectangleRoundedLinesEx(box, 0.5f, 8, 1, ui::theme.border);
    ui::TextEllipsis(toast_, Vector2{box.x + 18, box.y + (h - m.y) / 2}, w - 36, 14,
                     ui::theme.text);
}

void App::ImportM3uFile(const std::string& path) {
    const std::string fname = std::filesystem::path(path).filename().string();
    m3u::ImportResult res;
    if (!m3u::Import(path, library_, &res)) {
        Toast("Couldn't read " + fname);
        return;
    }
    if (res.trackIds.empty()) {
        Toast(TextFormat("%s: no matches for its %d entries — add the music folder first",
                         fname.c_str(), res.total));
        return;
    }
    Playlist& pl = playlists_.Create(res.name.empty() ? "Imported playlist" : res.name);
    pl.trackIds = std::move(res.trackIds);
    const std::string id = pl.id;
    const int matched = static_cast<int>(pl.trackIds.size());
    playlists_.Save();
    Toast(TextFormat("Imported %d of %d tracks from %s", matched, res.total, fname.c_str()));
    detailPlaylistId_ = id;
    plDetailScroll_ = 0;
    deleteArmId_.clear();
    view_ = View::PlaylistDetail;
}

void App::ExportPlaylist(const Playlist& p) {
    std::string name = p.name.empty() ? "playlist" : p.name;
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || static_cast<unsigned char>(c) < 32) c = '-';
    }
    const std::string path = paths::MusicDir() + "/" + name + ".m3u8";
    if (m3u::Export(path, ResolveTracks(p.trackIds))) {
        Toast("Exported to " + path);
    } else {
        Toast("Export failed: " + path);
    }
}

void App::DrawDebugOverlay(float chromeH) {
    const float W = static_cast<float>(GetScreenWidth());
    const float H = static_cast<float>(GetScreenHeight());
    const Rectangle box{W - 230, H - chromeH - 96, 218, 84};
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
