#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "raylib.h"

#include "config.h"
#include "depth.h"
#include "library.h"
#include "mosaic.h"
#include "player.h"
#include "playlist.h"
#include "vinyl.h"
#include "visualizer.h"

// Lazy GPU cache for album artwork. Views request textures while drawing;
// at most a couple of images are decoded+uploaded per frame to avoid hitches.
class ArtCache {
public:
    // Returns the texture if resident, otherwise queues it and returns nullptr.
    const Texture2D* Get(const Album& album);
    void ProcessQueue(int budget);
    bool HasPendingWork() const { return !wanted_.empty(); }
    void Clear();

private:
    std::unordered_map<std::string, Texture2D> textures_;
    std::unordered_set<std::string> failed_;
    std::unordered_map<std::string, std::string> wanted_;  // albumId -> artPath
};

class App {
public:
    void AddStartupFolder(const std::string& path) { startupFolders_.push_back(path); }
    // --import <file.m3u>: import a playlist at launch
    void AddStartupImport(const std::string& path) { startupImports_.push_back(path); }
    // --play: start playing the library on launch (handy for testing)
    void SetAutoplay(bool on) { autoplay_ = on; }
    // --shot <path>: capture the window to <path> ~2s after launch
    void SetScreenshotPath(const std::string& path) { screenshotPath_ = path; }
    // --view <library|albums|now>: start on a specific view (testing)
    void SetStartView(const std::string& name) { startView_ = name; }
    int Run();

private:
    enum class View { Library, Albums, AlbumDetail, Playlists, PlaylistDetail, NowPlaying };

    void Frame();
    void HandleInput();
    void HandleDroppedFolders();
    // Idle-aware pacing: 60 fps only when it matters, event-waiting when idle.
    void UpdatePacing();
    void MarkActivity() { lastActivity_ = GetTime(); }

    void DrawSidebar(Rectangle r);
    void DrawPlayerBar(Rectangle r);
    // Compact bar shown while browsing; click to expand into Now Playing.
    void DrawMiniPlayer(Rectangle r);
    void DrawLibraryView(Rectangle r);
    void DrawAlbumsView(Rectangle r);
    void DrawAlbumDetailView(Rectangle r);
    void DrawPlaylistsView(Rectangle r);
    void DrawPlaylistDetailView(Rectangle r);
    void DrawNowPlayingView(Rectangle r);
    void DrawEmptyState(Rectangle r);
    void DrawDebugOverlay();
    // Collapsible queue panel on the right edge; r is the revealed strip.
    void DrawQueuePanel(Rectangle r);
    // Right-click menu on track rows: favorites / Now Playing / playlists.
    void DrawTrackMenu();
    void OpenTrackMenu(const std::string& trackId);
    void DrawToast();

    void DrawAlbumArt(Rectangle r, const Album* album, float iconScale, float alpha = 1.0f);
    // Placeholder "cover" for playlists (rounded tile + glyph).
    void DrawPlaylistIcon(Rectangle r, const Playlist& p, float iconScale);
    struct TableResult {
        int clicked = -1;       // row to play
        int rightClicked = -1;  // row to open the context menu for
        int removed = -1;       // row whose remove button was clicked
    };
    TableResult DrawTrackTable(Rectangle r, const std::vector<const Track*>& tracks, float* scroll,
                               bool showAlbum, bool removable = false);
    void PlayFromTrackList(const std::vector<const Track*>& list, int index,
                           QueueSource source = QueueSource::Library, std::string sourceId = "");
    // Enables shuffle and plays the list starting from a random track.
    void ShufflePlay(const std::vector<const Track*>& list,
                     QueueSource source = QueueSource::Library, std::string sourceId = "");
    // Outlined "Shuffle" pill (30 tall) at (*x, y); advances *x past it.
    bool ShuffleButton(float* x, float y);
    // Accent "Shuffle All" pill at a view header's top-right corner.
    bool ShuffleAllButton(Rectangle r);
    std::vector<const Track*> ResolveTracks(const std::vector<std::string>& ids) const;
    void ImportM3uFile(const std::string& path);
    void ExportPlaylist(const Playlist& p);
    void ToggleQueuePanel();
    void Toast(const std::string& msg);

    MosaicSettings MosaicCfg() const;
    // Keeps the masked-foreground texture in sync with the playing album.
    void UpdateForeground();
    void BuildForeground(const Album& album);

    Config config_;
    Library library_;
    Playlists playlists_;
    Player player_{library_};
    Visualizer visualizer_;
    ArtCache art_;
    Mosaic mosaic_;
    DepthEngine depth_;
    Vinyl vinyl_;
    bool manualSkip_ = false;  // user-initiated track change this frame

    // Album art with the segmentation mask baked into its alpha channel
    struct Foreground {
        std::string albumId;       // album the texture belongs to
        std::string checkedAlbum;  // album we last looked for a mask for
        Texture2D tex{};
    } fg_;

    std::vector<std::string> startupFolders_;
    std::vector<std::string> startupImports_;

    View view_ = View::Library;
    std::string detailAlbumId_;
    std::string detailPlaylistId_;
    float libScroll_ = 0;
    float albumsScroll_ = 0;
    float detailScroll_ = 0;
    float playlistsScroll_ = 0;
    float plDetailScroll_ = 0;

    // Queue panel slide: 0 closed → 1 open; width follows the eased value.
    float queueAnim_ = 0;
    float queueScroll_ = 0;

    // Inline playlist rename (also entered right after New Playlist)
    std::string editPlaylistId_;
    std::string editText_;
    // Two-step delete: first click arms, second click deletes.
    std::string deleteArmId_;

    struct TrackMenu {
        bool open = false;
        bool justOpened = false;  // skip the opening click this frame
        Vector2 pos{};
        std::string trackId;
    } menu_;

    std::string toast_;
    double toastUntil_ = 0;

    bool seekDragging_ = false;
    float seekValue_ = 0;
    bool volumeDragging_ = false;

    double lastActivity_ = 0;
    int targetFps_ = 60;
    bool eventWaiting_ = false;
    bool showDebug_ = false;
    bool autoplay_ = false;
    std::string screenshotPath_;
    std::string startView_;
    int frameCount_ = 0;
    bool quitRequested_ = false;
};
