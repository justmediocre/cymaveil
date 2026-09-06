#include "app.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

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

// AppLayout.tsx geometry
constexpr float kSidebarW = 260.0f;
constexpr float kQueueW = 320.0f;
constexpr float kTitleBarH = 52.0f;
constexpr float kMiniPlayerH = 72.0f;
// Sidebar + queue + usable content: below this the two panels are exclusive.
constexpr float kNarrowThreshold = 1080.0f;
// Prefire the vinyl retract this many seconds before a track's natural end
// (usePlaybackCrossfade: max(crossfade, 1.5s)).
constexpr float kPrefireSeconds = 1.5f;

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

}  // namespace

// ── ArtCache ──

const Texture2D* ArtCache::Get(const std::string& artPath) {
    if (artPath.empty()) return nullptr;
    auto it = textures_.find(artPath);
    if (it != textures_.end()) return &it->second;
    if (!failed_.contains(artPath)) wanted_.insert(artPath);
    return nullptr;
}

void ArtCache::ProcessQueue(int budget) {
    while (budget-- > 0 && !wanted_.empty()) {
        auto it = wanted_.begin();
        Image img = LoadImage(it->c_str());
        if (img.data != nullptr) {
            Texture2D tex = LoadTextureFromImage(img);
            UnloadImage(img);
            GenTextureMipmaps(&tex);
            SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
            textures_[*it] = tex;
        } else {
            failed_.insert(*it);
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
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (!screenshotPath_.empty()) SetTraceLogLevel(LOG_DEBUG);
    g_logFile = std::fopen((paths::DataDir() + "/cymaveil.log").c_str(), "w");
    SetTraceLogCallback(FileTraceLog);
    crashlog::Install((paths::DataDir() + "/cymaveil.log").c_str());
    // Fixed-size in screenshot mode so tiling WMs float the window at the
    // requested resolution instead of fitting it into the layout, and no
    // HIGHDPI so shots come out at the exact requested pixel size on any
    // display. Interactively, HIGHDPI keeps layout in logical units.
    SetConfigFlags(screenshotPath_.empty()
                       ? (FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI)
                       : FLAG_MSAA_4X_HINT);
    InitWindow(1200, 800, "Cymaveil");
    Image icon = LoadImageFromMemory(".png", kIconPng, kIconPngSize);
    if (icon.data != nullptr) {
        ImageFormat(&icon, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        SetWindowIcon(icon);
        UnloadImage(icon);
    }
    // GLFW size limits are physical pixels on X11 but logical units on
    // Wayland; convert so the minimum stays 900x650 logical either way.
#ifdef __linux__
    const char* session = std::getenv("XDG_SESSION_TYPE");
    const bool onWayland =
        std::getenv("WAYLAND_DISPLAY") != nullptr &&
        !(session != nullptr && std::strcmp(session, "x11") == 0 && std::getenv("DISPLAY") != nullptr);
#else
    const bool onWayland = false;
#endif
    const Vector2 dpi = GetWindowScaleDPI();
    SetWindowMinSize(static_cast<int>(900 * (onWayland ? 1.0f : dpi.x)),
                     static_cast<int>(650 * (onWayland ? 1.0f : dpi.y)));
#ifndef __linux__
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
    sidebarAnim_ = config_.sidebarOpen ? 1.0f : 0.0f;
    queueAnim_ = config_.queuePanel ? 1.0f : 0.0f;
    RebuildMosaic();
    player_.SetVolume(config_.volume);
    player_.SetShuffle(config_.shuffle);
    player_.SetRepeat(static_cast<RepeatMode>(config_.repeat));
    player_.RestoreSession();
    prevVolume_ = config_.volume > 0.01f ? config_.volume : 0.75f;
    visualizer_.Attach();
    if (config_.mpris) mpris_.Start();
    watcher_.Start();

    for (const auto& f : startupFolders_) {
        if (DirectoryExists(f.c_str())) library_.AddFolder(f);
    }
    // An outdated cache keeps its folders but drops the tracks: rebuild it.
    if (startupFolders_.empty() && !library_.Folders().empty() && library_.Tracks().empty()) {
        library_.RequestRescan();
    }
    SyncWatcher();
    // AppLayout: open on Albums with an empty library, Now Playing otherwise.
    view_ = library_.Tracks().empty() ? View::Albums : (player_.HasTrack() ? View::NowPlaying : View::Library);
    if (view_ == View::NowPlaying) previousView_ = View::Library;
    if (startView_ == "library") view_ = View::Library;
    else if (startView_ == "search") view_ = View::Search;
    else if (startView_ == "albums") view_ = View::Albums;
    else if (startView_ == "favorites") view_ = View::Favorites;
    else if (startView_ == "playlists") view_ = View::Playlists;
    else if (startView_ == "now") view_ = View::NowPlaying;
    else if (startView_.rfind("settings", 0) == 0) {
        view_ = View::Settings;
        if (startView_ == "settings:playback") settingsTab_ = SettingsTab::Playback;
        else if (startView_ == "settings:visuals") settingsTab_ = SettingsTab::Visuals;
        else if (startView_ == "settings:depth") settingsTab_ = SettingsTab::DepthLayers;
        else if (startView_ == "settings:about") settingsTab_ = SettingsTab::About;
    }
    else if (startView_ == "brush") view_ = View::NowPlaying;
    else if (startView_ == "album" && !library_.Albums().empty()) OpenAlbum(library_.Albums().front().id);
    for (const auto& f : startupImports_) ImportM3uFile(f);
    MarkActivity();
    if (startFullscreen_) ToggleFullscreenMode();

    while (!WindowShouldClose() && !quitRequested_) Frame();

    config_.volume = player_.Volume();
    config_.shuffle = player_.Shuffle();
    config_.repeat = static_cast<int>(player_.Repeat());
    config_.Save();
    player_.SaveSession();

    mpris_.Stop();
    watcher_.Stop();
    player_.Shutdown();
    visualizer_.Detach();
    mosaic_.Unload();
    backdrop_.Unload();
    artView_.Unload();
    if (fg_.tex.id != 0) UnloadTexture(fg_.tex);
    if (brush_.artTex.id != 0) UnloadTexture(brush_.artTex);
    if (brush_.overlayTex.id != 0) UnloadTexture(brush_.overlayTex);
    art_.Clear();
    ui::Shutdown();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}

void App::RebuildMosaic() {
    mosaic_.Rebuild(library_.AllArt(), MosaicCfg());
    backdrop_.MarkDirty();
}

void App::Frame() {
    ui::NewFrame();
    HandleDroppedFolders();
    HandleInput();
    HandleMprisRequests();
    if (watcher_.Poll()) {
        library_.RequestRescan();
        MarkActivity();
    }
    player_.Update();
    if (library_.PollScan()) {
        RebuildMosaic();
        libGeneration_++;
        MarkActivity();
    }
    if (autoplay_ && !library_.Tracks().empty()) {
        autoplay_ = false;
        if (player_.HasTrack()) {
            if (!player_.IsPlaying()) player_.TogglePause();
        } else {
            PlayFromTrackList(AllTracksSorted(), 0);
        }
        if (startView_.empty()) Navigate(View::NowPlaying);
    }
    if (player_.IsPlaying() && GetTime() - sessionSaveAt_ > 10.0) {
        sessionSaveAt_ = GetTime();
        player_.SaveSession();
    }
    visualizer_.Update(GetFrameTime(), player_.IsPlaying());
    mosaic_.Update(GetFrameTime(), player_.IsPlaying(), MosaicCfg());
    if (art_.HasPendingWork()) backdrop_.MarkDirty();
    art_.ProcessQueue(2);

    // Now Playing art sequencing follows the current track's art; a track
    // about to end on a different album pre-fires the vinyl retract.
    {
        const Track* cur = player_.Current();
        const Album* album = cur != nullptr ? library_.AlbumById(cur->albumId) : nullptr;
        ArtView::Input in;
        in.target = ResolveArt(cur, album);
        in.targetAlbumId = cur != nullptr ? cur->albumId : std::string{};
        in.playing = player_.IsPlaying();
        in.skipIntent = manualSkip_;
        in.vinylEnabled = config_.vinylDisc;
        in.bassShake = config_.bassShake;
        in.bassEnergy = visualizer_.BassEnergy();
        if (player_.IsPlaying() && !seekDragging_ && player_.Repeat() != RepeatMode::One) {
            const float remaining = player_.TimeLength() - player_.TimePlayed();
            if (remaining > 0.0f && remaining <= kPrefireSeconds && player_.TimePlayed() > kPrefireSeconds) {
                const Track* next = player_.PeekNext();
                if (next != nullptr && next->albumId != in.targetAlbumId) in.prefire = true;
            }
        }
        artView_.Update(GetFrameTime(), in);
        manualSkip_ = false;
    }
    UpdateForeground();
    UpdateContour();

    if (startView_ == "brush" && !brush_.open) {
        const Art* a = artView_.HasDisplayed() ? &artView_.Displayed() : nullptr;
        if (a == nullptr) {
            for (const auto& al : library_.Albums())
                if (al.art.Valid()) {
                    a = &al.art;
                    break;
                }
        }
        if (a != nullptr && a->Valid()) {
            OpenBrushEditor(*a);
            startView_.clear();
        }
    }

    const float W = static_cast<float>(GetScreenWidth());
    const float H = static_cast<float>(GetScreenHeight());
    const bool hasTrack = player_.Current() != nullptr;
    const bool nowPlaying = view_ == View::NowPlaying && hasTrack;
    const bool immersive = fullscreen_ && nowPlaying;

    // Panels: width + opacity over 0.35s cubic-bezier(0.22, 1, 0.36, 1).
    const auto ease = [&](float& v, bool open) {
        const float target = open ? 1.0f : 0.0f;
        if (v != target) {
            const float step = std::min(GetFrameTime(), 1.0f / 30.0f) / 0.35f;
            v = Clamp(v + (v < target ? step : -step), 0.0f, 1.0f);
        }
        return ui::EaseOutExpo(v);
    };
    const bool sidebarOpen = config_.sidebarOpen && !immersive;
    const bool queueOpen = config_.queuePanel && !immersive && hasTrack;
    // Sidebar eases from its raw progress; the queue too. Opening one below the
    // narrow threshold closes the other (AppLayout NARROW_THRESHOLD).
    const float se = ease(sidebarAnim_, sidebarOpen);
    const float qe = ease(queueAnim_, queueOpen);
    const float sidebarW = kSidebarW * se;
    const float queueW = kQueueW * qe;
    const float titleH = immersive ? 0.0f : kTitleBarH;

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

    const Rectangle sidebar{0, 0, sidebarW, H};
    const Rectangle column{sidebarW, 0, W - sidebarW, H};
    const Rectangle titleBar{sidebarW, 0, W - sidebarW, titleH};
    const Rectangle queuePanel{W - queueW, titleH, queueW, H - titleH};
    const bool miniBar = !nowPlaying && hasTrack;
    const float barH = miniBar ? kMiniPlayerH : 0.0f;
    const Rectangle content{sidebarW, titleH, W - sidebarW - queueW, H - titleH - barH};
    const Rectangle bar{sidebarW, H - barH, W - sidebarW - queueW, barH};

    ui::BlockInput(menu_.open || brush_.open);

    backdrop_.EnsureSize(static_cast<int>(W), static_cast<int>(H));
    if (mosaic_.Animating()) backdrop_.MarkDirty();
    if (backdrop_.NeedsRender()) {
        backdrop_.BeginScene();
        ClearBackground(ui::theme.bg);
        mosaic_.Draw(Rectangle{0, 0, W, H}, art_, MosaicCfg(), ui::theme.bg);
        backdrop_.EndScene();
    }

    BeginDrawing();
    ClearBackground(ui::theme.bg);
    backdrop_.DrawScene();

    // Main column: frosted glass while browsing, transparent in Now Playing.
    if (!nowPlaying) backdrop_.DrawGlass(column, ui::theme.glassBg, config_.glassBlur);

    if (nowPlaying) {
        DrawNowPlayingView(content, immersive);
    } else {
        BeginScissorMode(static_cast<int>(content.x), static_cast<int>(content.y),
                         static_cast<int>(std::ceil(content.width)), static_cast<int>(content.height));
        switch (view_) {
            case View::Search: DrawSearchView(content); break;
            case View::Library: DrawLibraryView(content); break;
            case View::Albums: DrawAlbumsView(content); break;
            case View::AlbumDetail: DrawAlbumDetailView(content); break;
            case View::Favorites:
                detailPlaylistId_ = Playlists::kFavoritesId;
                DrawPlaylistDetailView(content);
                break;
            case View::Playlists: DrawPlaylistsView(content); break;
            case View::PlaylistDetail: DrawPlaylistDetailView(content); break;
            case View::Settings: DrawSettingsView(content); break;
            case View::NowPlaying: DrawEmptyState(content, "disc", "Nothing playing", "Pick a track to start"); break;
        }
        EndScissorMode();
        if (miniBar) DrawMiniPlayer(bar);
    }
    if (queueW > 0.5f) DrawQueuePanel(queuePanel);
    if (sidebarW > 0.5f) DrawSidebar(sidebar);
    if (!immersive) DrawTitleBar(titleBar);
    if (brush_.open) DrawBrushEditor(Rectangle{0, 0, W, H});
    DrawMenu();
    DrawToast(barH);
    if (showDebug_) DrawDebugOverlay(barH);

    EndDrawing();
    ui::BlockInput(false);

    if (!screenshotPath_.empty()) showDebug_ = true;
    if (!screenshotPath_.empty() && frameCount_ == 90) mosaic_.Trigger(MosaicCfg());
    if (!screenshotPath_.empty() && ++frameCount_ == 120) {
        TraceLog(LOG_INFO, "SHOT: screen %dx%d render %dx%d", GetScreenWidth(), GetScreenHeight(),
                 GetRenderWidth(), GetRenderHeight());
        Image shot = LoadImageFromScreen();
        ExportImage(shot, screenshotPath_.c_str());
        UnloadImage(shot);
        quitRequested_ = true;
    }
    PublishMpris();
    UpdatePacing();
}

void App::Navigate(View v) {
    if (v == View::NowPlaying) {
        if (view_ != View::NowPlaying) previousView_ = view_;
    }
    view_ = v;
    MarkActivity();
}

void App::OpenAlbum(const std::string& albumId) {
    detailAlbumId_ = albumId;
    detailScroll_ = 0;
    Navigate(View::AlbumDetail);
}

void App::OpenPlaylist(const std::string& playlistId) {
    detailPlaylistId_ = playlistId;
    plDetailScroll_ = 0;
    editPlaylistId_.clear();
    Navigate(playlistId == Playlists::kFavoritesId ? View::Favorites : View::PlaylistDetail);
}

void App::ToggleSidebar() {
    const bool willOpen = !config_.sidebarOpen;
    if (willOpen && config_.queuePanel && GetScreenWidth() < kNarrowThreshold) config_.queuePanel = false;
    config_.sidebarOpen = willOpen;
    MarkActivity();
}

void App::ToggleQueuePanel() {
    const bool willOpen = !config_.queuePanel;
    if (willOpen && config_.sidebarOpen && GetScreenWidth() < kNarrowThreshold) config_.sidebarOpen = false;
    config_.queuePanel = willOpen;
    MarkActivity();
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
                if (cur != nullptr && cur->id == req.str) player_.SeekTo(static_cast<float>(req.value));
                break;
            }
            case MprisCommand::SetVolume: player_.SetVolume(static_cast<float>(req.value)); break;
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
        const Album* a = library_.AlbumById(cur->albumId);
        if (a != nullptr) s.album = a->title;
        if (const Art* art = ResolveArt(cur, a); art != nullptr) s.artUrl = MprisFileUrl(art->path);
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
    while (GetKeyPressed() != 0) MarkActivity();

    if (brush_.open) return;
    // Text fields own the keyboard while focused.
    if (!editPlaylistId_.empty() || newPlaylistInput_ || (menu_.open && menu_.newInput)) return;
    if (view_ == View::Settings && folderInputActive_) {
        if (IsKeyPressed(KEY_ESCAPE)) folderInputActive_ = false;
        return;
    }
    if (menu_.open && IsKeyPressed(KEY_ESCAPE)) {
        menu_.open = false;
        ui::ConsumeKey(KEY_ESCAPE);
        return;
    }

    if (IsKeyPressed(KEY_F11)) {
        ToggleFullscreenMode();
        return;
    }
    if (fullscreen_ && IsKeyPressed(KEY_ESCAPE)) {
        ToggleFullscreenMode();
        ui::ConsumeKey(KEY_ESCAPE);
        return;
    }

    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
                      IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (ctrl && IsKeyPressed(KEY_F)) {
        Navigate(View::Search);
        return;
    }
    // On the Search view the always-focused input owns every printable key.
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
    if (IsKeyPressed(KEY_S) && !onSearch && !ctrl) player_.ToggleShuffle();
    if (IsKeyPressed(KEY_R) && !onSearch && !ctrl) player_.CycleRepeat();
    if ((ctrl && IsKeyPressed(KEY_T)) || (IsKeyPressed(KEY_Q) && !onSearch && !ctrl)) ToggleQueuePanel();
    // B opens the mask brush editor for the art on screen (EDIT_BRUSH).
    if (IsKeyPressed(KEY_B) && !menu_.open && !onSearch && !ctrl) {
        ui::ConsumeKey(KEY_B);
        if (artView_.HasDisplayed()) OpenBrushEditor(artView_.Displayed());
    }
    if (ctrl && shift && IsKeyPressed(KEY_B)) mosaic_.Trigger(MosaicCfg());  // ANIMATE_TILE
    if (!onSearch && !ctrl) {
        if (IsKeyPressed(KEY_ONE)) Navigate(View::Library);
        if (IsKeyPressed(KEY_TWO)) Navigate(View::Albums);
        if (IsKeyPressed(KEY_THREE)) Navigate(View::Playlists);
        if (IsKeyPressed(KEY_FOUR)) Navigate(View::NowPlaying);
        if (IsKeyPressed(KEY_COMMA)) Navigate(View::Settings);
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (view_ == View::AlbumDetail) Navigate(View::Albums);
        else if (view_ == View::PlaylistDetail) Navigate(View::Playlists);
        else if (view_ == View::NowPlaying) Navigate(previousView_);
        else if (view_ == View::Settings || view_ == View::Favorites) Navigate(View::Library);
    }
    if (IsKeyPressed(KEY_F3)) showDebug_ = !showDebug_;
}

MosaicSettings App::MosaicCfg() const {
    return MosaicSettings{config_.mosaicEnabled, config_.mosaicOpacity, config_.mosaicDensity,
                          config_.mosaicTransition, config_.mosaicFlat};
}

void App::ApplyTheme() {
    bool light = config_.theme == "light";
    if (config_.theme == "system") light = appearance::SystemScheme() == appearance::Scheme::Light;
    ui::ApplyTheme(light);
    backdrop_.MarkDirty();
    MarkActivity();
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
            const std::string parent = std::filesystem::path(p).parent_path().string();
            if (!parent.empty() && DirectoryExists(parent.c_str())) library_.AddFolder(parent);
        }
    }
    UnloadDroppedFiles(files);
    SyncWatcher();
    MarkActivity();
}

void App::UpdatePacing() {
    // The whole point of the rewrite: only burn CPU/GPU when something moves.
    const float ctrlTarget = (GetTime() - lastActivity_ < 2.5) ? 1.0f : 0.0f;
    const bool ctrlFading = fullscreen_ && view_ == View::NowPlaying && ctrlFade_ != ctrlTarget;
    const bool busy = library_.ScanActive() || art_.HasPendingWork() || seekDragging_ ||
                      volumeDragging_ || rangeDragging_ || mosaic_.Animating() || depth_.Busy() ||
                      artView_.Animating() || brush_.open || ui::MarqueeActive() || ui::Animating() ||
                      queueAnim_ != (config_.queuePanel ? 1.0f : 0.0f) ||
                      sidebarAnim_ != (config_.sidebarOpen ? 1.0f : 0.0f) ||
                      !editPlaylistId_.empty() || newPlaylistInput_ || folderInputActive_ ||
                      (menu_.open && menu_.newInput) || volumeOpen_ ||
                      ctrlFading || GetTime() < toastUntil_;
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
        TraceLog(LOG_INFO, "PACING: %s", wait ? "idle (event-wait)" : "active");
    }
    if (fps != targetFps_) {
        SetTargetFPS(fps);
        targetFps_ = fps;
    }
}

void App::ToggleFullscreenMode() {
    if (!fullscreen_) {
        const int monitor = MonitorForWindow();
        ToggleFullscreen();
        if (IsWindowFullscreen() && GetCurrentMonitor() != monitor) SetWindowMonitor(monitor);
    } else {
        ToggleFullscreen();
    }
    fullscreen_ = IsWindowFullscreen();
    if (cursorHidden_) {
        ShowCursor();
        cursorHidden_ = false;
    }
    MarkActivity();
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
    Navigate(View::NowPlaying);
}

void App::PlayAlbum(const std::string& albumId) {
    const auto tracks = library_.AlbumTracks(albumId);
    if (tracks.empty()) return;
    PlayFromTrackList(tracks, 0, QueueSource::Album, albumId);
}

std::vector<const Track*> App::ResolveTracks(const std::vector<std::string>& ids) const {
    std::vector<const Track*> out;
    out.reserve(ids.size());
    for (const auto& id : ids) {
        if (const Track* t = library_.TrackById(id)) out.push_back(t);
    }
    return out;
}

std::vector<const Track*> App::AllTracksSorted() const {
    std::vector<const Track*> all;
    all.reserve(library_.Tracks().size());
    for (const auto& t : library_.Tracks()) all.push_back(&t);
    return all;  // Library::tracks_ is already title-sorted
}

void App::Toast(const std::string& msg) {
    toast_ = msg;
    toastUntil_ = GetTime() + 3.5;
    MarkActivity();
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
    OpenPlaylist(id);
}

void App::ExportPlaylist(const Playlist& p) {
    std::string name = p.name.empty() ? "playlist" : p.name;
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || static_cast<unsigned char>(c) < 32) c = '-';
    }
    const std::string path = paths::MusicDir() + "/" + name + ".m3u8";
    if (m3u::Export(path, ResolveTracks(p.trackIds))) Toast("Exported to " + path);
    else Toast("Export failed: " + path);
}

void App::DrawDebugOverlay(float chromeH) {
    const float W = static_cast<float>(GetScreenWidth());
    const float H = static_cast<float>(GetScreenHeight());
    const Rectangle box{W - 236, H - chromeH - 100, 224, 88};
    ui::RoundedRect(box, 8, Fade(ui::theme.bg, 0.85f));
    ui::RoundedRectLines(box, 8, 1, ui::theme.border);
    const char* mode = eventWaiting_ ? "idle (event-wait)" : TextFormat("target %d fps", targetFps_);
    ui::Text(TextFormat("%d fps · %s", GetFPS(), mode), Vector2{box.x + 12, box.y + 10}, 12,
             ui::theme.text, ui::Face::Mono);
    ui::Text(TextFormat("frame %.2f ms", GetFrameTime() * 1000.0f), Vector2{box.x + 12, box.y + 28},
             12, ui::theme.textSecondary, ui::Face::Mono);
    ui::Text(TextFormat("%s · %.1f/%.1fs · bass %.2f", player_.IsPlaying() ? "playing" : "stopped",
                        player_.TimePlayed(), player_.TimeLength(), visualizer_.BassEnergy()),
             Vector2{box.x + 12, box.y + 46}, 12, ui::theme.textSecondary, ui::Face::Mono);
    ui::Text(TextFormat("depth: %s · fg %s", depth_.StatusText(), fg_.tex.id != 0 ? "ready" : "none"),
             Vector2{box.x + 12, box.y + 64}, 12, ui::theme.textSecondary, ui::Face::Mono);
}
