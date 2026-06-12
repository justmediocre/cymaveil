#include "library.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>

#include "colorextract.h"
#include "paths.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string HashId(const std::string& s) {
    // FNV-1a 64-bit
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

bool IsKnownUnsupported(const std::string& ext) {
    return ext == ".m4a" || ext == ".aac" || ext == ".opus" || ext == ".wma" || ext == ".aiff";
}

// Runs the ported web-app color extraction on embedded artwork bytes.
// Operates on CPU-side Image data (thread-safe).
AlbumColors ColorsFromArt(const unsigned char* bytes, int size, const char* ext) {
    AlbumColors colors;
    Image img = LoadImageFromMemory(ext, bytes, size);
    if (img.data == nullptr) return colors;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    ImageResize(&img, 64, 64);
    ExtractAlbumColors(static_cast<const unsigned char*>(img.data), &colors);
    UnloadImage(img);
    return colors;
}

}  // namespace

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool IsSupportedAudio(const std::string& ext) {
    return ext == ".mp3" || ext == ".flac" || ext == ".ogg" || ext == ".wav";
}

Library::~Library() {
    if (scanThread_.joinable()) scanThread_.join();
}

void Library::Load() {
    std::ifstream in(paths::LibraryFile());
    if (!in) return;
    try {
        json j = json::parse(in);
        folders_ = j.value("folders", std::vector<std::string>{});
        for (const auto& jt : j.value("tracks", json::array())) {
            Track t;
            t.id = jt.value("id", "");
            t.title = jt.value("title", "");
            t.artist = jt.value("artist", "");
            t.albumId = jt.value("albumId", "");
            t.filePath = jt.value("filePath", "");
            t.trackNum = jt.value("trackNum", 0);
            t.discNum = jt.value("discNum", 0);
            t.duration = jt.value("duration", 0.0f);
            t.mtime = jt.value("mtime", 0LL);
            tracks_.push_back(std::move(t));
        }
        for (const auto& ja : j.value("albums", json::array())) {
            Album a;
            a.id = ja.value("id", "");
            a.title = ja.value("title", "");
            a.artist = ja.value("artist", "");
            a.year = ja.value("year", 0);
            a.artPath = ja.value("artPath", "");
            const auto readColor = [&ja](const char* key, Color fallback) {
                const auto c = ja.value(key, std::vector<int>{});
                if (c.size() != 3) return fallback;
                return Color{static_cast<unsigned char>(c[0]), static_cast<unsigned char>(c[1]),
                             static_cast<unsigned char>(c[2]), 255};
            };
            a.dominant = readColor("dominant", a.dominant);
            // Caches written before accent extraction fall back to dominant
            a.accent = readColor("accent", a.dominant);
            a.accentSecondary = readColor("accent2", Color{0, 0, 0, 255});
            a.hasSecondary = ja.contains("accent2");
            albums_.push_back(std::move(a));
        }
        SortAndIndex();
    } catch (const std::exception&) {
        tracks_.clear();
        albums_.clear();
    }
}

void Library::Save() const {
    json jt = json::array();
    for (const auto& t : tracks_) {
        jt.push_back({{"id", t.id},
                      {"title", t.title},
                      {"artist", t.artist},
                      {"albumId", t.albumId},
                      {"filePath", t.filePath},
                      {"trackNum", t.trackNum},
                      {"discNum", t.discNum},
                      {"duration", t.duration},
                      {"mtime", t.mtime}});
    }
    json ja = json::array();
    for (const auto& a : albums_) {
        json entry{{"id", a.id},
                   {"title", a.title},
                   {"artist", a.artist},
                   {"year", a.year},
                   {"artPath", a.artPath},
                   {"dominant", {a.dominant.r, a.dominant.g, a.dominant.b}},
                   {"accent", {a.accent.r, a.accent.g, a.accent.b}}};
        if (a.hasSecondary) {
            entry["accent2"] = {a.accentSecondary.r, a.accentSecondary.g, a.accentSecondary.b};
        }
        ja.push_back(std::move(entry));
    }
    json j{{"folders", folders_}, {"tracks", std::move(jt)}, {"albums", std::move(ja)}};
    std::ofstream out(paths::LibraryFile());
    out << j.dump() << '\n';
}

void Library::AddFolder(const std::string& path) {
    std::error_code ec;
    const std::string canon = fs::weakly_canonical(fs::path(path), ec).string();
    const std::string& folder = ec ? path : canon;
    if (std::find(folders_.begin(), folders_.end(), folder) == folders_.end()) {
        folders_.push_back(folder);
        scanGeneration_++;  // any in-flight scan now has a stale folder view
    }
    RequestRescan();
}

void Library::RemoveFolder(const std::string& path) {
    auto it = std::find(folders_.begin(), folders_.end(), path);
    if (it == folders_.end()) return;
    folders_.erase(it);
    scanGeneration_++;  // any in-flight scan now has a stale folder view
    if (folders_.empty()) {
        // Nothing left to scan: drop everything and persist the empty library.
        // (A rescan with no folders would early-out and leave stale tracks.)
        tracks_.clear();
        albums_.clear();
        SortAndIndex();
        Save();
        return;
    }
    // An incremental rescan drops tracks under the removed folder for free:
    // they're no longer walked, so they're neither reused nor re-parsed.
    RequestRescan();
}

void Library::RequestRescan() {
    if (ScanActive()) {
        rescanQueued_ = true;  // current scan has a stale view of the folders
    } else {
        StartScan();
    }
}

void Library::StartScan() {
    if (scanActive_.load() || folders_.empty()) return;
    if (scanThread_.joinable()) scanThread_.join();
    scanActive_ = true;
    scanDone_ = false;
    scanCurrent_ = 0;
    scanTotal_ = 0;
    scanSkipped_ = 0;
    // Copy the current tracks/albums into the worker so unchanged files can be
    // reused. tracks_/albums_ stay readable on the main thread during the scan.
    scanThread_ = std::thread(&Library::ScanWorker, this, scanGeneration_, folders_, tracks_,
                              albums_);
}

ScanStatus Library::Status() const {
    return ScanStatus{scanCurrent_.load(), scanTotal_.load(), scanSkipped_.load()};
}

bool Library::PollScan() {
    if (!scanDone_.load()) return false;
    scanDone_ = false;
    bool stale;
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        // Folders changed after this scan started (e.g. its only folder was
        // removed). Discarding the results keeps the removed folder's tracks
        // out of the library instead of resurrecting them.
        stale = pendingGeneration_ != scanGeneration_;
        if (!stale) {
            tracks_ = std::move(pendingTracks_);
            albums_ = std::move(pendingAlbums_);
        }
        pendingTracks_.clear();
        pendingAlbums_.clear();
    }
    if (stale) {
        // A queued rescan would early-out if folders_ is now empty, so the
        // RemoveFolder-empty path already cleared and persisted the library.
        if (rescanQueued_) {
            rescanQueued_ = false;
            StartScan();
        }
        return false;
    }
    SortAndIndex();
    Save();
    if (rescanQueued_) {
        rescanQueued_ = false;
        StartScan();
    }
    return true;
}

const Track* Library::TrackById(const std::string& id) const {
    auto it = trackIdx_.find(id);
    return it == trackIdx_.end() ? nullptr : &tracks_[it->second];
}

const Album* Library::AlbumById(const std::string& id) const {
    auto it = albumIdx_.find(id);
    return it == albumIdx_.end() ? nullptr : &albums_[it->second];
}

std::vector<const Track*> Library::AlbumTracks(const std::string& albumId) const {
    std::vector<const Track*> out;
    for (const auto& t : tracks_) {
        if (t.albumId == albumId) out.push_back(&t);
    }
    // tracks_ is sorted by title, which scrambles album order —
    // within an album, disc/track number is the order that matters.
    std::sort(out.begin(), out.end(), [](const Track* a, const Track* b) {
        if (a->discNum != b->discNum) return a->discNum < b->discNum;
        if (a->trackNum != b->trackNum) return a->trackNum < b->trackNum;
        return Lower(a->title) < Lower(b->title);
    });
    return out;
}

void Library::SortAndIndex() {
    std::sort(tracks_.begin(), tracks_.end(), [](const Track& a, const Track& b) {
        if (a.title != b.title) return Lower(a.title) < Lower(b.title);
        return Lower(a.artist) < Lower(b.artist);
    });
    std::sort(albums_.begin(), albums_.end(), [](const Album& a, const Album& b) {
        if (a.artist != b.artist) return Lower(a.artist) < Lower(b.artist);
        if (a.year != b.year) return a.year < b.year;
        return Lower(a.title) < Lower(b.title);
    });
    trackIdx_.clear();
    albumIdx_.clear();
    for (size_t i = 0; i < tracks_.size(); i++) trackIdx_[tracks_[i].id] = i;
    for (size_t i = 0; i < albums_.size(); i++) albumIdx_[albums_[i].id] = i;
}

void Library::ScanWorker(unsigned generation, std::vector<std::string> folders,
                         std::vector<Track> oldTracks, std::vector<Album> oldAlbums) {
    // Index the prior cache so unchanged files can be reused verbatim instead
    // of paying for another TagLib parse + artwork/color extraction.
    std::unordered_map<std::string, const Track*> oldByPath;
    oldByPath.reserve(oldTracks.size());
    for (const auto& t : oldTracks) oldByPath[t.filePath] = &t;
    std::unordered_map<std::string, const Album*> oldAlbumById;
    oldAlbumById.reserve(oldAlbums.size());
    for (const auto& a : oldAlbums) oldAlbumById[a.id] = &a;

    struct FoundFile {
        fs::path path;
        long long mtime;
    };
    std::vector<FoundFile> files;
    for (const auto& folder : folders) {
        std::error_code ec;
        fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec);
        if (ec) continue;
        for (const auto& entry : it) {
            if (!entry.is_regular_file(ec)) continue;
            const std::string ext = Lower(entry.path().extension().string());
            if (IsSupportedAudio(ext)) {
                const auto wt = fs::last_write_time(entry.path(), ec);
                const long long mtime =
                    ec ? 0 : static_cast<long long>(wt.time_since_epoch().count());
                files.push_back({entry.path(), mtime});
            } else if (IsKnownUnsupported(ext)) {
                scanSkipped_++;
            }
        }
    }
    scanTotal_ = static_cast<int>(files.size());

    std::vector<Track> tracks;
    std::unordered_map<std::string, Album> albums;
    const std::string artDir = paths::ArtDir();

    for (const auto& found : files) {
        const fs::path& path = found.path;
        scanCurrent_++;

        // Unchanged since the last scan? Reuse the cached track and its album —
        // but only if the album's cached art still exists on disk. If the art
        // cache was deleted, fall through to a full parse so it gets
        // re-extracted from the file's embedded artwork.
        if (const auto cached = oldByPath.find(path.string());
            cached != oldByPath.end() && found.mtime != 0 && cached->second->mtime == found.mtime) {
            const Track& old = *cached->second;
            const Album* oldAlbum = nullptr;
            if (const auto oa = oldAlbumById.find(old.albumId); oa != oldAlbumById.end()) {
                oldAlbum = oa->second;
            }
            std::error_code artEc;
            const bool artMissing =
                oldAlbum && !oldAlbum->artPath.empty() && !fs::exists(oldAlbum->artPath, artEc);
            if (!artMissing) {
                if (oldAlbum && !albums.count(old.albumId)) {
                    albums[old.albumId] = *oldAlbum;
                }
                tracks.push_back(old);
                continue;
            }
        }

        TagLib::FileRef f(path.string().c_str(), true, TagLib::AudioProperties::Average);
        if (f.isNull() || f.file() == nullptr) continue;

        Track t;
        t.filePath = path.string();
        t.id = HashId(t.filePath);
        t.mtime = found.mtime;

        std::string albumTitle = "Unknown Album";
        std::string albumArtist;
        if (const TagLib::Tag* tag = f.tag()) {
            t.title = tag->title().to8Bit(true);
            t.artist = tag->artist().to8Bit(true);
            t.trackNum = static_cast<int>(tag->track());
            if (!tag->album().isEmpty()) albumTitle = tag->album().to8Bit(true);

            const auto props = f.file()->properties();
            if (props.contains("ALBUMARTIST")) {
                albumArtist = props["ALBUMARTIST"].front().to8Bit(true);
            }
            if (props.contains("DISCNUMBER")) {
                t.discNum = std::atoi(props["DISCNUMBER"].front().to8Bit(true).c_str());
            }
            if (albumArtist.empty()) albumArtist = t.artist;

            const std::string albumKey = Lower(albumArtist) + "\x1f" + Lower(albumTitle);
            t.albumId = HashId(albumKey);

            auto [it, inserted] = albums.try_emplace(t.albumId);
            Album& album = it->second;
            if (inserted) {
                album.id = t.albumId;
                album.title = albumTitle;
                album.artist = albumArtist.empty() ? "Unknown Artist" : albumArtist;
            }
            if (album.year == 0 && tag->year() > 0) album.year = static_cast<int>(tag->year());

            if (album.artPath.empty()) {
                const auto pictures = f.file()->complexProperties("PICTURE");
                if (!pictures.isEmpty()) {
                    const auto& pic = pictures.front();
                    const auto data = pic.value("data").value<TagLib::ByteVector>();
                    if (!data.isEmpty()) {
                        const std::string mime = pic.value("mimeType").value<TagLib::String>().to8Bit(true);
                        const char* ext = (mime.find("png") != std::string::npos) ? ".png" : ".jpg";
                        const std::string artPath = artDir + "/" + album.id + ext;
                        std::ofstream out(artPath, std::ios::binary);
                        if (out.write(data.data(), static_cast<std::streamsize>(data.size()))) {
                            album.artPath = artPath;
                            const AlbumColors colors = ColorsFromArt(
                                reinterpret_cast<const unsigned char*>(data.data()),
                                static_cast<int>(data.size()), ext);
                            album.dominant = colors.dominant;
                            album.accent = colors.accent;
                            album.accentSecondary = colors.accentSecondary;
                            album.hasSecondary = colors.hasSecondary;
                        }
                    }
                }
            }
        } else {
            t.albumId = HashId("\x1funknown");
            auto [it, inserted] = albums.try_emplace(t.albumId);
            if (inserted) {
                it->second.id = t.albumId;
                it->second.title = "Unknown Album";
                it->second.artist = "Unknown Artist";
            }
        }

        if (t.title.empty()) t.title = path.stem().string();
        if (t.artist.empty()) t.artist = "Unknown Artist";
        if (const TagLib::AudioProperties* ap = f.audioProperties()) {
            t.duration = static_cast<float>(ap->lengthInMilliseconds()) / 1000.0f;
        }
        tracks.push_back(std::move(t));
    }

    // Backfill artwork/palette from the prior cache for any album that ended up
    // without art — e.g. its only art-bearing track was reused (so never
    // re-extracted) but the album entry was first created by a different track.
    for (auto& [id, album] : albums) {
        if (!album.artPath.empty()) continue;
        const auto oa = oldAlbumById.find(id);
        if (oa == oldAlbumById.end() || oa->second->artPath.empty()) continue;
        // Don't resurrect a deleted art cache — its file must still exist.
        std::error_code artEc;
        if (!fs::exists(oa->second->artPath, artEc)) continue;
        album.artPath = oa->second->artPath;
        album.dominant = oa->second->dominant;
        album.accent = oa->second->accent;
        album.accentSecondary = oa->second->accentSecondary;
        album.hasSecondary = oa->second->hasSecondary;
        if (album.year == 0) album.year = oa->second->year;
    }

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        pendingGeneration_ = generation;
        pendingTracks_ = std::move(tracks);
        pendingAlbums_.clear();
        pendingAlbums_.reserve(albums.size());
        for (auto& [id, album] : albums) pendingAlbums_.push_back(std::move(album));
    }
    scanActive_ = false;
    scanDone_ = true;
}
