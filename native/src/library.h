#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "raylib.h"

struct Track {
    std::string id;
    std::string title;
    std::string artist;
    std::string albumId;
    std::string filePath;
    int trackNum = 0;
    int discNum = 0;
    float duration = 0.0f;  // seconds
};

struct Album {
    std::string id;
    std::string title;
    std::string artist;
    int year = 0;
    std::string artPath;  // extracted artwork file; empty if none
    Color dominant{110, 110, 122, 255};
};

struct ScanStatus {
    int current = 0;
    int total = 0;
    int skipped = 0;  // unsupported formats (m4a/opus/...)
};

// Music library: scanning happens on a worker thread, results are merged on
// the main thread via PollScan(). Persisted as JSON alongside extracted art.
class Library {
public:
    ~Library();

    void Load();
    void Save() const;

    // Adds folder (deduped) and kicks off a full rescan.
    void AddFolder(const std::string& path);
    void StartScan();
    bool ScanActive() const { return scanActive_.load(); }
    ScanStatus Status() const;
    // Merges finished scan results; returns true on the frame a scan lands.
    bool PollScan();

    const std::vector<Track>& Tracks() const { return tracks_; }
    const std::vector<Album>& Albums() const { return albums_; }
    const std::vector<std::string>& Folders() const { return folders_; }

    const Track* TrackById(const std::string& id) const;
    const Album* AlbumById(const std::string& id) const;
    std::vector<const Track*> AlbumTracks(const std::string& albumId) const;

private:
    void SortAndIndex();
    void ScanWorker(std::vector<std::string> folders);

    std::vector<Track> tracks_;
    std::vector<Album> albums_;
    std::vector<std::string> folders_;
    std::unordered_map<std::string, size_t> trackIdx_;
    std::unordered_map<std::string, size_t> albumIdx_;

    std::thread scanThread_;
    std::atomic<bool> scanActive_{false};
    std::atomic<bool> scanDone_{false};
    std::atomic<int> scanCurrent_{0};
    std::atomic<int> scanTotal_{0};
    std::atomic<int> scanSkipped_{0};
    std::mutex resultMutex_;
    std::vector<Track> pendingTracks_;
    std::vector<Album> pendingAlbums_;
};
