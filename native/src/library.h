#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "raylib.h"

// Lowercases an ASCII string (used for case-insensitive sorting and matching
// file extensions). Shared so the folder watcher can match the same set of
// supported audio extensions without duplicating the logic.
std::string Lower(std::string s);

// True if ext (a lowercased extension including the leading dot, e.g. ".mp3")
// is one of the audio formats the scanner imports.
bool IsSupportedAudio(const std::string& ext);

struct Track {
    std::string id;
    std::string title;
    std::string artist;
    std::string albumId;
    std::string filePath;
    int trackNum = 0;
    int discNum = 0;
    float duration = 0.0f;  // seconds
    // Filesystem mtime when last parsed (filesystem-clock ticks). Lets a rescan
    // skip files that haven't changed since the cache was written.
    long long mtime = 0;
};

struct Album {
    std::string id;
    std::string title;
    std::string artist;
    int year = 0;
    std::string artPath;  // extracted artwork file; empty if none
    // Palette extracted from the artwork (see colorextract.h)
    Color dominant{110, 110, 122, 255};        // darkened, for background washes
    Color accent{212, 165, 116, 255};          // vivid, for visualizer glow
    Color accentSecondary{0, 0, 0, 255};       // distinct hue, for two-tone bar cores
    bool hasSecondary = false;
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

    // Adds folder (deduped) and kicks off a rescan.
    void AddFolder(const std::string& path);
    // Drops a folder and its tracks; rescans (or clears, if it was the last).
    void RemoveFolder(const std::string& path);
    // Re-scan the current folders. Safe to call mid-scan (queues a follow-up).
    // The scan is incremental: unchanged files are reused from the cache.
    void RequestRescan();
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
    // Carries snapshots of the prior tracks/albums so unchanged files can be
    // reused instead of re-parsed. Snapshots are copied in on the main thread.
    // generation identifies the folder-set view this scan was started for; its
    // results are discarded if the folders change before the scan lands.
    void ScanWorker(unsigned generation, std::vector<std::string> folders,
                    std::vector<Track> oldTracks, std::vector<Album> oldAlbums);

    std::vector<Track> tracks_;
    std::vector<Album> albums_;
    std::vector<std::string> folders_;
    std::unordered_map<std::string, size_t> trackIdx_;
    std::unordered_map<std::string, size_t> albumIdx_;

    std::thread scanThread_;
    bool rescanQueued_ = false;  // folder added mid-scan; rescan when it lands
    // Bumped whenever the folder set changes. The worker stamps its results with
    // the generation it started at; PollScan() drops results from a stale
    // generation so a removed folder's tracks can't be resurrected.
    unsigned scanGeneration_ = 0;
    std::atomic<bool> scanActive_{false};
    std::atomic<bool> scanDone_{false};
    std::atomic<int> scanCurrent_{0};
    std::atomic<int> scanTotal_{0};
    std::atomic<int> scanSkipped_{0};
    std::mutex resultMutex_;
    std::vector<Track> pendingTracks_;
    std::vector<Album> pendingAlbums_;
    unsigned pendingGeneration_ = 0;  // generation the pending results were scanned for
};
