#include "playlist.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "library.h"
#include "paths.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

Playlist* Playlists::Find(const std::string& id) {
    for (auto& p : playlists_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

const Playlist* Playlists::ById(const std::string& id) const {
    for (const auto& p : playlists_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

void Playlists::Load() {
    playlists_.clear();
    std::ifstream in(paths::PlaylistsFile());
    if (in) {
        try {
            json j = json::parse(in);
            for (const auto& jp : j.value("playlists", json::array())) {
                Playlist p;
                p.id = jp.value("id", "");
                p.name = jp.value("name", "");
                p.trackIds = jp.value("trackIds", std::vector<std::string>{});
                p.createdAt = jp.value("createdAt", 0ll);
                if (!p.id.empty()) playlists_.push_back(std::move(p));
            }
        } catch (const std::exception&) {
            playlists_.clear();
        }
    }
    // System playlists: Favorites pinned first, Now Playing pinned last.
    if (Find(kFavoritesId) == nullptr) {
        playlists_.insert(playlists_.begin(), Playlist{kFavoritesId, "Favorites", {}, 0});
    }
    if (Find(kNowPlayingId) == nullptr) {
        playlists_.push_back(Playlist{kNowPlayingId, "Now Playing", {}, 0});
    }
}

void Playlists::Save() const {
    json jp = json::array();
    for (const auto& p : playlists_) {
        jp.push_back({{"id", p.id},
                      {"name", p.name},
                      {"trackIds", p.trackIds},
                      {"createdAt", p.createdAt}});
    }
    std::ofstream out(paths::PlaylistsFile());
    out << json{{"playlists", std::move(jp)}}.dump() << '\n';
}

Playlist& Playlists::Create(const std::string& name) {
    Playlist p;
    p.createdAt = NowMs();
    p.id = "playlist-" + std::to_string(p.createdAt);
    p.name = name;
    // Keep Now Playing last so the sidebar/list order stays stable.
    playlists_.insert(playlists_.end() - 1, std::move(p));
    Save();
    return playlists_[playlists_.size() - 2];
}

void Playlists::Remove(const std::string& id) {
    if (IsSystem(id)) return;
    std::erase_if(playlists_, [&](const Playlist& p) { return p.id == id; });
    Save();
}

void Playlists::Rename(const std::string& id, const std::string& name) {
    if (IsSystem(id) || name.empty()) return;
    if (Playlist* p = Find(id)) {
        p->name = name;
        Save();
    }
}

bool Playlists::AddTrack(const std::string& playlistId, const std::string& trackId) {
    Playlist* p = Find(playlistId);
    if (p == nullptr || Contains(playlistId, trackId)) return false;
    p->trackIds.push_back(trackId);
    Save();
    return true;
}

void Playlists::RemoveTrack(const std::string& playlistId, const std::string& trackId) {
    if (Playlist* p = Find(playlistId)) {
        std::erase(p->trackIds, trackId);
        Save();
    }
}

bool Playlists::Contains(const std::string& playlistId, const std::string& trackId) const {
    const Playlist* p = ById(playlistId);
    return p != nullptr &&
           std::find(p->trackIds.begin(), p->trackIds.end(), trackId) != p->trackIds.end();
}

void Playlists::ToggleFavorite(const std::string& trackId) {
    if (IsFavorite(trackId)) {
        RemoveTrack(kFavoritesId, trackId);
    } else {
        AddTrack(kFavoritesId, trackId);
    }
}

// ── M3U ──

namespace m3u {

namespace {

std::string CanonicalOrSelf(const fs::path& p) {
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical(p, ec);
    return ec ? p.string() : canon.string();
}

}  // namespace

bool Import(const std::string& path, const Library& lib, ImportResult* out) {
    std::ifstream in(path);
    if (!in) return false;

    // Library paths come from the scanner; canonicalize both sides so
    // playlist entries match even through symlinks or ../ segments.
    std::unordered_map<std::string, std::string> byPath;
    for (const auto& t : lib.Tracks()) byPath[CanonicalOrSelf(t.filePath)] = t.id;

    out->name = fs::path(path).stem().string();
    out->trackIds.clear();
    out->total = 0;

    const fs::path baseDir = fs::path(path).parent_path();
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        // Strip a UTF-8 BOM on the first line
        if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        if (line.empty() || line[0] == '#') continue;
        out->total++;
        fs::path entry(line);
        if (entry.is_relative()) entry = baseDir / entry;
        auto it = byPath.find(CanonicalOrSelf(entry));
        if (it != byPath.end()) out->trackIds.push_back(it->second);
    }
    return true;
}

bool Export(const std::string& path, const std::vector<const Track*>& tracks) {
    std::ofstream outFile(path);
    if (!outFile) return false;
    outFile << "#EXTM3U\n";
    for (const Track* t : tracks) {
        outFile << "#EXTINF:" << static_cast<int>(t->duration) << "," << t->title << '\n';
        outFile << t->filePath << '\n';
    }
    return static_cast<bool>(outFile);
}

}  // namespace m3u
