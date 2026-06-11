#include "library.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>

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

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool IsSupportedAudio(const std::string& ext) {
    return ext == ".mp3" || ext == ".flac" || ext == ".ogg" || ext == ".wav";
}

bool IsKnownUnsupported(const std::string& ext) {
    return ext == ".m4a" || ext == ".aac" || ext == ".opus" || ext == ".wma" || ext == ".aiff";
}

// Average pixel color weighted by saturation, so artwork accents win over
// large flat dark/light areas. Operates on CPU-side Image data (thread-safe).
Color DominantColor(const unsigned char* bytes, int size, const char* ext) {
    Image img = LoadImageFromMemory(ext, bytes, size);
    if (img.data == nullptr) return Color{110, 110, 122, 255};
    ImageResize(&img, 32, 32);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const auto* px = static_cast<const unsigned char*>(img.data);
    double r = 0, g = 0, b = 0, wsum = 0;
    for (int i = 0; i < 32 * 32; i++) {
        const float pr = px[i * 4 + 0], pg = px[i * 4 + 1], pb = px[i * 4 + 2];
        const float mx = std::max({pr, pg, pb}), mn = std::min({pr, pg, pb});
        const float sat = mx > 0 ? (mx - mn) / mx : 0.0f;
        const float w = 0.15f + sat;  // never zero so grayscale art still averages
        r += pr * w;
        g += pg * w;
        b += pb * w;
        wsum += w;
    }
    UnloadImage(img);
    if (wsum <= 0) return Color{110, 110, 122, 255};
    return Color{static_cast<unsigned char>(r / wsum), static_cast<unsigned char>(g / wsum),
                 static_cast<unsigned char>(b / wsum), 255};
}

}  // namespace

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
            tracks_.push_back(std::move(t));
        }
        for (const auto& ja : j.value("albums", json::array())) {
            Album a;
            a.id = ja.value("id", "");
            a.title = ja.value("title", "");
            a.artist = ja.value("artist", "");
            a.year = ja.value("year", 0);
            a.artPath = ja.value("artPath", "");
            const auto c = ja.value("dominant", std::vector<int>{110, 110, 122});
            if (c.size() == 3) {
                a.dominant = Color{static_cast<unsigned char>(c[0]), static_cast<unsigned char>(c[1]),
                                   static_cast<unsigned char>(c[2]), 255};
            }
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
                      {"duration", t.duration}});
    }
    json ja = json::array();
    for (const auto& a : albums_) {
        ja.push_back({{"id", a.id},
                      {"title", a.title},
                      {"artist", a.artist},
                      {"year", a.year},
                      {"artPath", a.artPath},
                      {"dominant", {a.dominant.r, a.dominant.g, a.dominant.b}}});
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
    }
    StartScan();
}

void Library::StartScan() {
    if (scanActive_.load() || folders_.empty()) return;
    if (scanThread_.joinable()) scanThread_.join();
    scanActive_ = true;
    scanDone_ = false;
    scanCurrent_ = 0;
    scanTotal_ = 0;
    scanSkipped_ = 0;
    scanThread_ = std::thread(&Library::ScanWorker, this, folders_);
}

ScanStatus Library::Status() const {
    return ScanStatus{scanCurrent_.load(), scanTotal_.load(), scanSkipped_.load()};
}

bool Library::PollScan() {
    if (!scanDone_.load()) return false;
    scanDone_ = false;
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        tracks_ = std::move(pendingTracks_);
        albums_ = std::move(pendingAlbums_);
        pendingTracks_.clear();
        pendingAlbums_.clear();
    }
    SortAndIndex();
    Save();
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
    return out;
}

void Library::SortAndIndex() {
    std::sort(tracks_.begin(), tracks_.end(), [](const Track& a, const Track& b) {
        if (a.artist != b.artist) return Lower(a.artist) < Lower(b.artist);
        if (a.albumId != b.albumId) return a.albumId < b.albumId;
        if (a.discNum != b.discNum) return a.discNum < b.discNum;
        if (a.trackNum != b.trackNum) return a.trackNum < b.trackNum;
        return Lower(a.title) < Lower(b.title);
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

void Library::ScanWorker(std::vector<std::string> folders) {
    std::vector<fs::path> files;
    for (const auto& folder : folders) {
        std::error_code ec;
        fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec);
        if (ec) continue;
        for (const auto& entry : it) {
            if (!entry.is_regular_file(ec)) continue;
            const std::string ext = Lower(entry.path().extension().string());
            if (IsSupportedAudio(ext)) {
                files.push_back(entry.path());
            } else if (IsKnownUnsupported(ext)) {
                scanSkipped_++;
            }
        }
    }
    scanTotal_ = static_cast<int>(files.size());

    std::vector<Track> tracks;
    std::unordered_map<std::string, Album> albums;
    const std::string artDir = paths::ArtDir();

    for (const auto& path : files) {
        scanCurrent_++;
        TagLib::FileRef f(path.string().c_str(), true, TagLib::AudioProperties::Average);
        if (f.isNull() || f.file() == nullptr) continue;

        Track t;
        t.filePath = path.string();
        t.id = HashId(t.filePath);

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
                            album.dominant = DominantColor(
                                reinterpret_cast<const unsigned char*>(data.data()),
                                static_cast<int>(data.size()), ext);
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

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        pendingTracks_ = std::move(tracks);
        pendingAlbums_.clear();
        pendingAlbums_.reserve(albums.size());
        for (auto& [id, album] : albums) pendingAlbums_.push_back(std::move(album));
    }
    scanActive_ = false;
    scanDone_ = true;
}
