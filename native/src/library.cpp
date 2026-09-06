#include "library.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>

#include "colorextract.h"
#include "paths.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

// fs::path::string() encodes in the OS-native narrow charset — UTF-8 on Linux,
// but the ANSI codepage on Windows, which is not valid UTF-8 for non-ASCII
// names. We persist paths as JSON (which requires UTF-8) and key the cache on
// them, so always go through UTF-8. u8string() yields UTF-8 on every platform.
std::string Utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

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

// Bumped when the on-disk cache shape changes incompatibly. v2: hash-named
// art files + per-track art; older caches are discarded and rebuilt by a
// full rescan (the art directory is re-populated from the files' tags).
constexpr int kSchemaVersion = 2;

// Content hash of the artwork bytes: identical covers across an album's
// tracks (or across albums) share one file on disk, and a track only gets
// its own art when this differs from the album's.
std::string ArtHash(const unsigned char* bytes, int size) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < size; i++) {
        h ^= bytes[i];
        h *= 1099511628211ull;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

json ArtToJson(const Art& a) {
    json j{{"path", a.path},
           {"dominant", {a.dominant.r, a.dominant.g, a.dominant.b}},
           {"accent", {a.accent.r, a.accent.g, a.accent.b}}};
    if (a.hasSecondary) j["accent2"] = {a.accentSecondary.r, a.accentSecondary.g, a.accentSecondary.b};
    return j;
}

Art ArtFromJson(const json& j) {
    Art a;
    if (!j.is_object()) return a;
    a.path = j.value("path", "");
    const auto readColor = [&j](const char* key, Color fallback) {
        const auto c = j.value(key, std::vector<int>{});
        if (c.size() != 3) return fallback;
        return Color{static_cast<unsigned char>(c[0]), static_cast<unsigned char>(c[1]),
                     static_cast<unsigned char>(c[2]), 255};
    };
    a.dominant = readColor("dominant", a.dominant);
    a.accent = readColor("accent", a.dominant);
    a.accentSecondary = readColor("accent2", Color{0, 0, 0, 255});
    a.hasSecondary = j.contains("accent2");
    return a;
}

bool IsKnownUnsupported(const std::string& ext) {
    return ext == ".m4a" || ext == ".aac" || ext == ".opus" || ext == ".wma" || ext == ".aiff";
}

// Probe PNG/JPEG header dimensions without decoding. Returns false when the
// dimensions are unreadable or large enough that width*height*4 overflows
// raylib's int pixel-size math — that under-allocates the buffer, the decoder
// then overruns it and corrupts the heap (a hard crash with no catchable
// exception, seen as a ucrtbase.dll fault). Unknown formats pass through to the
// decoder, which enforces its own per-dimension limits.
bool ArtDimensionsSafe(const unsigned char* b, int size) {
    auto be16 = [&](int o) { return (b[o] << 8) | b[o + 1]; };
    auto be32 = [&](int o) {
        return (static_cast<long long>(b[o]) << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3];
    };
    long long w = 0, h = 0;
    if (size >= 24 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') {
        // PNG: IHDR is the first chunk after the 8-byte signature; width/height
        // are the first two big-endian u32s of its data.
        w = be32(16);
        h = be32(20);
    } else if (size >= 4 && b[0] == 0xFF && b[1] == 0xD8) {
        // JPEG: walk the marker segments to the start-of-frame, which carries
        // the frame dimensions.
        for (int p = 2; p + 9 < size;) {
            if (b[p] != 0xFF) {
                p++;
                continue;
            }
            const int marker = b[p + 1];
            const int len = be16(p + 2);
            if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 &&
                marker != 0xCC) {
                h = be16(p + 5);
                w = be16(p + 7);
                break;
            }
            if (len < 2) break;
            p += 2 + len;
        }
    } else {
        return true;
    }
    if (w <= 0 || h <= 0) return false;
    // 64M px (×4 = 256 MB) sits well below INT_MAX and far above any real cover.
    return w <= 16384 && h <= 16384 && w * h <= 64'000'000;
}

// Runs the ported web-app color extraction on embedded artwork bytes.
// Operates on CPU-side Image data (thread-safe).
AlbumColors ColorsFromArt(const unsigned char* bytes, int size, const char* ext) {
    AlbumColors colors;
    if (bytes == nullptr || size <= 0) return colors;
    Image img = LoadImageFromMemory(ext, bytes, size);
    // Bail on anything raylib couldn't decode into a sane bitmap — feeding a
    // zero/garbage-sized image into the resize + color extraction below would
    // read out of bounds.
    if (img.data == nullptr || img.width <= 0 || img.height <= 0) {
        UnloadImage(img);
        return colors;
    }
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    ImageResize(&img, 64, 64);
    if (img.data != nullptr) ExtractAlbumColors(static_cast<const unsigned char*>(img.data), &colors);
    UnloadImage(img);
    return colors;
}

}  // namespace

std::string ArtKey(const std::string& artPath) {
    if (artPath.empty()) return "";
    return fs::path(artPath).stem().string();
}

const Art* ResolveArt(const Track* track, const Album* album) {
    if (track != nullptr && track->art.Valid()) return &track->art;
    if (album != nullptr && album->art.Valid()) return &album->art;
    return nullptr;
}

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
        if (j.value("version", 1) != kSchemaVersion) {
            // Old cache: keep the folder list so the rescan below rebuilds
            // everything, but don't load tracks/albums shaped for the old art model.
            TraceLog(LOG_INFO, "LIBRARY: cache schema is outdated, rebuilding on next scan");
            return;
        }
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
            if (jt.contains("art")) t.art = ArtFromJson(jt["art"]);
            tracks_.push_back(std::move(t));
        }
        for (const auto& ja : j.value("albums", json::array())) {
            Album a;
            a.id = ja.value("id", "");
            a.title = ja.value("title", "");
            a.artist = ja.value("artist", "");
            a.year = ja.value("year", 0);
            if (ja.contains("art")) a.art = ArtFromJson(ja["art"]);
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
        json entry{{"id", t.id},
                   {"title", t.title},
                   {"artist", t.artist},
                   {"albumId", t.albumId},
                   {"filePath", t.filePath},
                   {"trackNum", t.trackNum},
                   {"discNum", t.discNum},
                   {"duration", t.duration},
                   {"mtime", t.mtime}};
        if (t.art.Valid()) entry["art"] = ArtToJson(t.art);
        jt.push_back(std::move(entry));
    }
    json ja = json::array();
    for (const auto& a : albums_) {
        json entry{{"id", a.id}, {"title", a.title}, {"artist", a.artist}, {"year", a.year}};
        if (a.art.Valid()) entry["art"] = ArtToJson(a.art);
        ja.push_back(std::move(entry));
    }
    json j{{"version", kSchemaVersion},
           {"folders", folders_},
           {"tracks", std::move(jt)},
           {"albums", std::move(ja)}};
    std::ofstream out(paths::LibraryFile());
    // error_handler::replace: never throw on a stray non-UTF-8 byte (e.g. a path
    // from a codepage we didn't normalize) — substitute U+FFFD instead. An
    // uncaught dump() throw here was crashing the whole app at scan completion.
    out << j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
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

std::vector<const Art*> Library::AllArt() const {
    std::vector<const Art*> out;
    std::unordered_set<std::string> seen;
    for (const auto& a : albums_) {
        if (a.art.Valid() && seen.insert(a.art.path).second) out.push_back(&a.art);
    }
    for (const auto& t : tracks_) {
        if (t.art.Valid() && seen.insert(t.art.path).second) out.push_back(&t.art);
    }
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
        // Drive iteration with the non-throwing increment(ec). The range-for's
        // operator++ throws filesystem_error on a mid-tree error (long paths
        // >MAX_PATH, junctions/reparse points, transient I/O — far more common
        // on Windows), and in this worker thread that uncaught throw would call
        // std::terminate. Stop this tree on error and keep what we collected.
        const fs::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            const fs::directory_entry& entry = *it;
            std::error_code fileEc;
            if (!entry.is_regular_file(fileEc)) continue;
            const std::string ext = Lower(entry.path().extension().string());
            if (IsSupportedAudio(ext)) {
                const auto wt = fs::last_write_time(entry.path(), fileEc);
                const long long mtime =
                    fileEc ? 0 : static_cast<long long>(wt.time_since_epoch().count());
                files.push_back({entry.path(), mtime});
            } else if (IsKnownUnsupported(ext)) {
                scanSkipped_++;
            }
        }
    }
    scanTotal_ = static_cast<int>(files.size());
    TraceLog(LOG_INFO, "LIBRARY: scanning %d files", scanTotal_.load());

    std::vector<Track> tracks;
    std::unordered_map<std::string, Album> albums;
    std::unordered_map<std::string, Art> palettes;  // artPath -> palette, per scan
    const std::string artDir = paths::ArtDir();

    for (const auto& found : files) {
        const fs::path& path = found.path;
        scanCurrent_++;
        if (scanCurrent_ % 250 == 0)
            TraceLog(LOG_INFO, "LIBRARY: scanned %d/%d", scanCurrent_.load(), scanTotal_.load());

        // One malformed file must not take down the scan worker: an uncaught
        // exception here (TagLib, std, ...) would abort the whole process
        // (seen as a ucrtbase.dll fault on Windows). Skip the file instead.
        try {
            // Unchanged since the last scan? Reuse the cached track and its album
            // — but only if the album's cached art still exists on disk. If the
            // art cache was deleted, fall through to a full parse so it gets
            // re-extracted from the file's embedded artwork.
            if (const auto cached = oldByPath.find(Utf8(path));
                cached != oldByPath.end() && found.mtime != 0 &&
                cached->second->mtime == found.mtime) {
                const Track& old = *cached->second;
                const Album* oldAlbum = nullptr;
                if (const auto oa = oldAlbumById.find(old.albumId); oa != oldAlbumById.end()) {
                    oldAlbum = oa->second;
                }
                std::error_code artEc;
                const bool artMissing =
                    (oldAlbum && oldAlbum->art.Valid() && !fs::exists(oldAlbum->art.path, artEc)) ||
                    (old.art.Valid() && !fs::exists(old.art.path, artEc));
                if (!artMissing) {
                    if (oldAlbum && !albums.count(old.albumId)) {
                        albums[old.albumId] = *oldAlbum;
                    }
                    tracks.push_back(old);
                    continue;
                }
            }

            // path.c_str() is wchar_t* on Windows / char* elsewhere; TagLib's
            // FileName takes the matching overload, so non-ASCII paths open
            // without depending on the process codepage.
            TagLib::FileRef f(path.c_str(), true, TagLib::AudioProperties::Average);
            if (f.isNull() || f.file() == nullptr) continue;

            Track t;
            t.filePath = Utf8(path);
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
                if (props.contains("ALBUMARTIST") && !props["ALBUMARTIST"].isEmpty()) {
                    albumArtist = props["ALBUMARTIST"].front().to8Bit(true);
                }
                if (props.contains("DISCNUMBER") && !props["DISCNUMBER"].isEmpty()) {
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

                // Extract the embedded picture for every file: the first one
                // becomes the album cover, and a track whose picture differs
                // from the album's keeps it as its own art. Files are named by
                // content hash so identical covers share one file.
                const auto pictures = f.file()->complexProperties("PICTURE");
                if (!pictures.isEmpty()) {
                    const auto& pic = pictures.front();
                    const auto data = pic.value("data").value<TagLib::ByteVector>();
                    const auto* artBytes = reinterpret_cast<const unsigned char*>(data.data());
                    const int artSize = static_cast<int>(data.size());
                    if (data.isEmpty() || !ArtDimensionsSafe(artBytes, artSize)) {
                        if (!data.isEmpty()) {
                            TraceLog(LOG_WARNING,
                                     "LIBRARY: skipping oversized/unreadable art in %s",
                                     path.string().c_str());
                        }
                    } else {
                        const std::string hash = ArtHash(artBytes, artSize);
                        const std::string mime =
                            pic.value("mimeType").value<TagLib::String>().to8Bit(true);
                        const char* ext = (mime.find("png") != std::string::npos) ? ".png" : ".jpg";
                        const std::string artPath = artDir + "/art-" + hash + ext;
                        const bool albumHasArt = album.art.Valid();
                        const bool sameAsAlbum = albumHasArt && album.art.path == artPath;
                        if (!sameAsAlbum) {
                            Art art;
                            std::error_code exEc;
                            bool ok = fs::exists(artPath, exEc);
                            if (!ok) {
                                std::ofstream out(artPath, std::ios::binary);
                                ok = static_cast<bool>(
                                    out.write(data.data(), static_cast<std::streamsize>(data.size())));
                            }
                            if (ok) {
                                art.path = artPath;
                                // Reuse a palette already computed for this file this scan.
                                auto pal = palettes.find(artPath);
                                if (pal == palettes.end()) {
                                    const AlbumColors colors = ColorsFromArt(artBytes, artSize, ext);
                                    art.dominant = colors.dominant;
                                    art.accent = colors.accent;
                                    art.accentSecondary = colors.accentSecondary;
                                    art.hasSecondary = colors.hasSecondary;
                                    palettes.emplace(artPath, art);
                                } else {
                                    art = pal->second;
                                }
                                if (!albumHasArt) album.art = art;
                                else t.art = art;
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

            if (t.title.empty()) t.title = Utf8(path.stem());
            if (t.artist.empty()) t.artist = "Unknown Artist";
            if (const TagLib::AudioProperties* ap = f.audioProperties()) {
                t.duration = static_cast<float>(ap->lengthInMilliseconds()) / 1000.0f;
            }
            tracks.push_back(std::move(t));
        } catch (const std::exception& e) {
            // Don't call path.string() here — it can itself throw on Windows for
            // un-encodable names, which would escape the worker.
            TraceLog(LOG_WARNING, "LIBRARY: skipped a file: %s", e.what());
        }
    }

    // Backfill artwork/palette from the prior cache for any album that ended up
    // without art — e.g. its only art-bearing track was reused (so never
    // re-extracted) but the album entry was first created by a different track.
    for (auto& [id, album] : albums) {
        if (album.art.Valid()) continue;
        const auto oa = oldAlbumById.find(id);
        if (oa == oldAlbumById.end() || !oa->second->art.Valid()) continue;
        // Don't resurrect a deleted art cache — its file must still exist.
        std::error_code artEc;
        if (!fs::exists(oa->second->art.path, artEc)) continue;
        album.art = oa->second->art;
        if (album.year == 0) album.year = oa->second->year;
    }
    // A track whose own art turned out to be the album cover after all (the
    // album picked its cover from a later file) drops the redundant copy.
    for (auto& t : tracks) {
        if (!t.art.Valid()) continue;
        const auto it = albums.find(t.albumId);
        if (it != albums.end() && it->second.art.path == t.art.path) t.art = Art{};
    }

    TraceLog(LOG_INFO, "LIBRARY: scan complete — %zu tracks, %zu albums", tracks.size(),
             albums.size());
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
