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
    // --play: start playing the library on launch (handy for testing)
    void SetAutoplay(bool on) { autoplay_ = on; }
    // --shot <path>: capture the window to <path> ~2s after launch
    void SetScreenshotPath(const std::string& path) { screenshotPath_ = path; }
    int Run();

private:
    enum class View { Library, Albums, AlbumDetail, NowPlaying };

    void Frame();
    void HandleInput();
    void HandleDroppedFolders();
    // Idle-aware pacing: 60 fps only when it matters, event-waiting when idle.
    void UpdatePacing();
    void MarkActivity() { lastActivity_ = GetTime(); }

    void DrawSidebar(Rectangle r);
    void DrawPlayerBar(Rectangle r);
    void DrawLibraryView(Rectangle r);
    void DrawAlbumsView(Rectangle r);
    void DrawAlbumDetailView(Rectangle r);
    void DrawNowPlayingView(Rectangle r);
    void DrawEmptyState(Rectangle r);
    void DrawDebugOverlay();

    void DrawAlbumArt(Rectangle r, const Album* album, float iconScale, float alpha = 1.0f);
    // Returns the index of a clicked row, or -1.
    int DrawTrackTable(Rectangle r, const std::vector<const Track*>& tracks, float* scroll,
                       bool showAlbum);
    void PlayFromTrackList(const std::vector<const Track*>& list, int index);

    MosaicSettings MosaicCfg() const;
    // Keeps the masked-foreground texture in sync with the playing album.
    void UpdateForeground();
    void BuildForeground(const Album& album);

    Config config_;
    Library library_;
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

    View view_ = View::Library;
    std::string detailAlbumId_;
    float libScroll_ = 0;
    float albumsScroll_ = 0;
    float detailScroll_ = 0;

    bool seekDragging_ = false;
    float seekValue_ = 0;
    bool volumeDragging_ = false;

    double lastActivity_ = 0;
    int targetFps_ = 60;
    bool eventWaiting_ = false;
    bool showDebug_ = false;
    bool autoplay_ = false;
    std::string screenshotPath_;
    int frameCount_ = 0;
};
