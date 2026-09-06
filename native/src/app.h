#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "raylib.h"

#include "artview.h"
#include "backdrop.h"
#include "brush.h"
#include "config.h"
#include "contour.h"
#include "depth.h"
#include "library.h"
#include "mosaic.h"
#include "mpris.h"
#include "player.h"
#include "playlist.h"
#include "visualizer.h"
#include "watcher.h"

// Lazy GPU cache for album artwork, keyed by artwork file path. Views request
// textures while drawing; at most a couple of images are decoded+uploaded per
// frame to avoid hitches.
class ArtCache {
public:
    // Returns the texture if resident, otherwise queues it and returns nullptr.
    const Texture2D* Get(const std::string& artPath);
    const Texture2D* Get(const Art* art) { return art != nullptr ? Get(art->path) : nullptr; }
    void ProcessQueue(int budget);
    bool HasPendingWork() const { return !wanted_.empty(); }
    void Clear();

private:
    std::unordered_map<std::string, Texture2D> textures_;
    std::unordered_set<std::string> failed_;
    std::unordered_set<std::string> wanted_;
};

// The application: window, frame loop, and every view. Drawing is split over
// app_chrome.cpp (sidebar, title bar, mini player, queue, menus), app_browse.cpp
// (library/albums/playlists/search/settings), app_nowplaying.cpp and
// app_brush.cpp (mask painting); app.cpp owns the loop, input and state.
class App {
public:
    void AddStartupFolder(const std::string& path) { startupFolders_.push_back(path); }
    // --import <file.m3u>: import a playlist at launch
    void AddStartupImport(const std::string& path) { startupImports_.push_back(path); }
    // --play: start playing the library on launch (handy for testing)
    void SetAutoplay(bool on) { autoplay_ = on; }
    // --fullscreen: enter fullscreen on launch (testing; F11 does this interactively)
    void SetStartFullscreen(bool on) { startFullscreen_ = on; }
    // --shot <path>: capture the window to <path> ~2s after launch
    void SetScreenshotPath(const std::string& path) { screenshotPath_ = path; }
    // --view <search|library|albums|now>: start on a specific view (testing)
    void SetStartView(const std::string& name) { startView_ = name; }
    // --size <W>x<H>: initial window size in logical units (testing narrow layouts)
    void SetStartSize(int w, int h) { startW_ = w; startH_ = h; }
    int Run();

private:
    enum class View {
        Search, Library, Albums, AlbumDetail, Favorites, Playlists, PlaylistDetail, NowPlaying,
        Settings
    };
    enum class SettingsTab { Library, Playback, Visuals, DepthLayers, About };

    void Frame();
    void HandleInput();
    void HandleDroppedFolders();
    // Hands the watcher the current folder set after any add/remove.
    void SyncWatcher() { watcher_.SetFolders(library_.Folders()); }
    // Drains commands from the MPRIS worker (media keys, desktop applets).
    void HandleMprisRequests();
    // Pushes the current player state to the MPRIS worker (cheap when idle).
    void PublishMpris();
    // Idle-aware pacing: 60 fps only when it matters, event-waiting when idle.
    void UpdatePacing();
    void MarkActivity() { lastActivity_ = GetTime(); }
    // Borderless-fullscreen toggle (F11). Now Playing goes immersive in it.
    void ToggleFullscreenMode();
    // Navigation with the web app's "previous view" memory for Now Playing.
    void Navigate(View v);
    void OpenAlbum(const std::string& albumId);
    void OpenPlaylist(const std::string& playlistId);
    void ToggleSidebar();
    void ToggleQueuePanel();
    void RebuildMosaic();

    // ── Chrome (app_chrome.cpp) ──
    void DrawSidebar(Rectangle r);
    void DrawTitleBar(Rectangle r);
    // Compact bar shown while browsing; click to expand into Now Playing.
    void DrawMiniPlayer(Rectangle r);
    // Collapsible queue panel on the right edge; r is the revealed strip.
    void DrawQueuePanel(Rectangle r);
    // Popup menus (add-to-playlist / right-click track menu / settings selects).
    struct MenuItem {
        std::string label;
        std::function<void()> fn;
        bool checked = false;
        bool separatorAbove = false;
        bool secondary = false;  // dimmer text (e.g. "+ New Playlist")
    };
    void OpenMenu(Rectangle anchor, std::vector<MenuItem> items, const std::string& header = "",
                  bool preferAbove = true);
    void OpenTrackMenu(const std::string& trackId, Rectangle anchor, bool preferAbove);
    void DrawMenu();
    // chromeH is the bottom chrome height the toast floats clear of.
    void DrawToast(float chromeH);
    void Toast(const std::string& msg);
    void DrawDebugOverlay(float chromeH);

    // ── Browse views (app_browse.cpp) ──
    void DrawSearchView(Rectangle r);
    void DrawLibraryView(Rectangle r);
    void DrawAlbumsView(Rectangle r);
    void DrawAlbumDetailView(Rectangle r);
    void DrawPlaylistsView(Rectangle r);
    void DrawPlaylistDetailView(Rectangle r);
    void DrawSettingsView(Rectangle r);
    void DrawEmptyState(Rectangle r, const char* icon, const char* title, const char* subtitle);
    // Page header: font-display 24px title, count, right-aligned actions.
    // Returns the y just below the header block.
    float DrawPageHeader(Rectangle r, const std::string& title, const std::string& count);
    // Back link ("‹ Albums"); returns true when clicked.
    bool BackLink(Vector2 pos, const std::string& label, const std::string& key);
    struct TableResult {
        int clicked = -1;       // row to play
        int removed = -1;       // row whose remove button was clicked
    };
    // TrackList/TrackRow port: 44px rows with number/eq, 32px art, title,
    // artist, hover actions (heart, add-to-playlist, optional remove) and
    // duration. Owns scrolling and clipping.
    TableResult DrawTrackList(Rectangle r, const std::vector<const Track*>& tracks, float* scroll,
                              bool removable, bool autoScroll = false,
                              const std::vector<std::string>* letters = nullptr);
    // Scrollable auto-fill grid of AlbumCards inside r.
    void DrawAlbumGrid(Rectangle r, const std::vector<const Album*>& albums, float* scroll);
    void DrawAlbumArt(Rectangle r, const Art* art, float radius, float placeholderIcon = 0);
    void PlayFromTrackList(const std::vector<const Track*>& list, int index,
                           QueueSource source = QueueSource::Library, std::string sourceId = "");
    // Enables shuffle and plays the list starting from a random track.
    void ShufflePlay(const std::vector<const Track*>& list,
                     QueueSource source = QueueSource::Library, std::string sourceId = "");
    void PlayAlbum(const std::string& albumId);
    std::vector<const Track*> ResolveTracks(const std::vector<std::string>& ids) const;
    std::vector<const Track*> AllTracksSorted() const;
    void ImportM3uFile(const std::string& path);
    void ExportPlaylist(const Playlist& p);
    void ShowNewPlaylistInput();

    // ── Now Playing (app_nowplaying.cpp) ──
    void DrawNowPlayingView(Rectangle r, bool immersive);
    // Keeps the masked-foreground texture in sync with the shown art.
    void UpdateForeground();
    void BuildForeground(const Art& art);
    void UpdateContour();
    Visualizer::Style ResolvedStyle();
    void CycleVisualizer();

    // ── Manual mask painting (app_brush.cpp) ──
    void OpenBrushEditor(const Art& art);
    void CloseBrushEditor();
    void DrawBrushEditor(Rectangle r);
    void RebuildBrushOverlay();
    void SaveBrushMask();

    MosaicSettings MosaicCfg() const;
    // Resolves config_.theme ("system"/"light"/"dark") and applies the palette.
    void ApplyTheme();

    Config config_;
    Library library_;
    Playlists playlists_;
    Player player_{library_};
    Visualizer visualizer_;
    ArtCache art_;
    Mosaic mosaic_;
    Backdrop backdrop_;
    DepthEngine depth_;
    ArtView artView_;
    Mpris mpris_;
    FolderWatcher watcher_;
    bool manualSkip_ = false;  // user-initiated track change this frame

    // Album art with the segmentation mask baked into its alpha channel
    struct Foreground {
        std::string artKey;     // art the texture belongs to
        std::string checkedKey; // art we last looked for a mask for
        Texture2D tex{};
    } fg_;
    // Contour for the contour-bars style, keyed by art path.
    struct Contour {
        std::string artPath;
        ContourData data;
    } contour_;

    // Manual mask painting overlay; opened from Now Playing.
    struct Brush {
        bool open = false;
        BrushCanvas canvas;
        Art art;
        Texture2D artTex{};      // album art base layer
        Texture2D overlayTex{};  // mask preview composite
        std::vector<unsigned char> overlayBuf;  // scratch for texture updates
        bool overlayDirty = true;
        bool painting = false;
        Vector2 lastMask{};
    } brush_;

    std::vector<std::string> startupFolders_;
    std::vector<std::string> startupImports_;

    View view_ = View::Library;
    View previousView_ = View::Library;  // where "Back" from Now Playing returns
    std::string detailAlbumId_;
    std::string detailPlaylistId_;
    SettingsTab settingsTab_ = SettingsTab::Library;
    // Search filters across track titles, albums, and artists.
    std::string searchQuery_;
    float searchAlbumsScroll_ = 0;
    float searchTracksScroll_ = 0;
    unsigned libGeneration_ = 0;
    std::string searchCacheKey_;
    unsigned searchCacheGen_ = 0;
    bool searchCacheValid_ = false;
    std::vector<const Album*> searchAlbums_;
    std::vector<const Track*> searchTracks_;
    float libScroll_ = 0;
    float albumsScroll_ = 0;
    float detailScroll_ = 0;
    float favScroll_ = 0;
    float playlistsScroll_ = 0;
    float plDetailScroll_ = 0;
    float settingsScroll_ = 0;
    float queueScroll_ = 0;
    std::string queueFollowId_;
    float queueScrollTarget_ = 0;
    int queueScrollFollow_ = 0;  // 0 idle, 1 easing, 2 jump
    float settingsContentH_ = 0;
    std::string deleteArmId_;    // two-step playlist delete

    // Panel open/close progress (0..1), eased like the web's width animations.
    float sidebarAnim_ = 1;
    float queueAnim_ = 0;

    // Inline playlist rename / creation
    std::string editPlaylistId_;
    std::string editText_;
    bool newPlaylistInput_ = false;
    std::string newPlaylistText_;
    // Settings → Music Folders: typed-path add field and its focus state.
    std::string folderInput_;
    bool folderInputActive_ = false;

    struct Menu {
        bool open = false;
        bool justOpened = false;
        Rectangle anchor{};
        bool preferAbove = true;
        std::string header;
        std::vector<MenuItem> items;
        // Inline "+ New Playlist" text field inside the add-to-playlist menu
        bool newInput = false;
        std::string newText;
        std::string trackId;
    } menu_;

    std::string toast_;
    double toastUntil_ = 0;

    bool seekDragging_ = false;
    float seekValue_ = 0;
    bool volumeDragging_ = false;
    bool volumeOpen_ = false;
    double volumeCloseAt_ = 0;
    float prevVolume_ = 0.75f;
    bool rangeDragging_ = false;
    int rangeDragId_ = -1;

    // Visualizer style: 'random' is resolved per track; clicking the art
    // cycles styles for the current track.
    Visualizer::Style randomPick_ = Visualizer::Style::FullSurface;
    bool hasRandomPick_ = false;
    std::string randomPickTrack_;
    int manualPick_ = -1;
    std::string manualPickTrack_;

    double lastActivity_ = 0;
    double sessionSaveAt_ = 0;  // last playback-session checkpoint
    int targetFps_ = 60;
    bool eventWaiting_ = false;
    bool showDebug_ = false;
    bool fullscreen_ = false;
    bool cursorHidden_ = false;
    float ctrlFade_ = 1.0f;
    bool autoplay_ = false;
    bool startFullscreen_ = false;
    std::string screenshotPath_;
    std::string startView_;
    int startW_ = 1200;
    int startH_ = 800;
    int frameCount_ = 0;
    bool quitRequested_ = false;
};
