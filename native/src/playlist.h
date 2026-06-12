#pragma once

#include <string>
#include <vector>

class Library;
struct Track;

struct Playlist {
    std::string id;
    std::string name;
    std::vector<std::string> trackIds;
    long long createdAt = 0;  // ms since epoch; 0 for system playlists
};

// User playlists plus the two system playlists ported from the web app:
// Favorites (always first) and Now Playing (always last). Persisted as JSON;
// mutations save immediately — they're user-paced and the file is tiny.
class Playlists {
public:
    static constexpr const char* kFavoritesId = "favorites";
    static constexpr const char* kNowPlayingId = "now-playing";

    void Load();  // creates the system playlists if missing
    void Save() const;

    const std::vector<Playlist>& All() const { return playlists_; }
    const Playlist* ById(const std::string& id) const;
    Playlist& Favorites() { return *Find(kFavoritesId); }
    Playlist& NowPlaying() { return *Find(kNowPlayingId); }
    static bool IsSystem(const std::string& id) {
        return id == kFavoritesId || id == kNowPlayingId;
    }

    Playlist& Create(const std::string& name);
    void Remove(const std::string& id);          // no-op for system playlists
    void Rename(const std::string& id, const std::string& name);  // ditto
    // Appends if absent; returns true when the track was added.
    bool AddTrack(const std::string& playlistId, const std::string& trackId);
    void RemoveTrack(const std::string& playlistId, const std::string& trackId);
    bool Contains(const std::string& playlistId, const std::string& trackId) const;

    bool IsFavorite(const std::string& trackId) const { return Contains(kFavoritesId, trackId); }
    void ToggleFavorite(const std::string& trackId);

private:
    Playlist* Find(const std::string& id);
    std::vector<Playlist> playlists_;
};

namespace m3u {

struct ImportResult {
    std::string name;                 // playlist file stem
    std::vector<std::string> trackIds;  // library tracks matched by file path
    int total = 0;                    // entries in the file (for "x of y matched")
};

// Parses an .m3u/.m3u8, resolves relative entries against the file's folder,
// and matches them to library tracks by canonical path. Returns false when
// the file can't be read.
bool Import(const std::string& path, const Library& lib, ImportResult* out);

// Writes an extended M3U8 (#EXTM3U / #EXTINF) UTF-8 file. Returns success.
bool Export(const std::string& path, const std::vector<const Track*>& tracks);

}  // namespace m3u
