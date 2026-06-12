#pragma once

#include <string>
#include <vector>

#include "raylib.h"

class Library;
struct Track;

enum class RepeatMode { Off, All, One };

// What the active queue was built from; lets the queue panel label itself
// and lets Now Playing playlist edits stay in sync with playback.
enum class QueueSource { None, Library, Album, Playlist, NowPlaying };

// Playback engine on top of raylib's streaming Music API.
// Owns the queue; shuffle is an index indirection (order_) over queue_.
class Player {
public:
    explicit Player(Library& lib) : lib_(lib) {}

    void Shutdown();
    // Per-frame: feeds the stream and auto-advances when a track ends.
    void Update();

    void PlayQueue(std::vector<std::string> trackIds, int startIndex,
                   QueueSource source = QueueSource::None, std::string sourceId = "");
    void TogglePause();
    // MPRIS Stop: halt and rewind, keep the queue so Play resumes here.
    void Stop();
    void Next() { Advance(1, true); }
    void Prev();
    void SeekTo(float seconds);

    void SetVolume(float v);
    float Volume() const { return volume_; }
    void ToggleShuffle();
    bool Shuffle() const { return shuffle_; }
    void CycleRepeat();
    void SetShuffle(bool on) { shuffle_ = on; }
    void SetRepeat(RepeatMode m) { repeat_ = m; }
    RepeatMode Repeat() const { return repeat_; }

    bool IsPlaying() const { return state_ == State::Playing; }
    bool IsStopped() const { return state_ == State::Stopped; }
    bool HasTrack() const { return loaded_; }
    const Track* Current() const;
    float TimePlayed() const;
    float TimeLength() const;

    // Queue introspection for the queue panel, all in play order (i.e. the
    // shuffled order when shuffle is on).
    QueueSource Source() const { return source_; }
    const std::string& SourceId() const { return sourceId_; }
    int QueueSize() const { return static_cast<int>(order_.size()); }
    int OrderPos() const { return orderPos_; }
    const Track* TrackAtOrderPos(int pos) const;
    void JumpTo(int orderPos);   // play the queue entry at this position
    void RemoveAt(int orderPos);
    // Removes the first queue entry with this id (Now Playing list sync).
    void RemoveTrackId(const std::string& trackId);
    // Appends to the live queue (no-op when no queue is active).
    void Append(const std::string& trackId);
    void ClearQueue();

private:
    enum class State { Stopped, Playing, Paused };

    bool LoadCurrent(bool autoplay);
    void Advance(int dir, bool manual);
    void BuildOrder(int firstQueueIndex);
    void UnloadCurrent();

    Library& lib_;
    Music music_{};
    bool loaded_ = false;
    State state_ = State::Stopped;
    std::vector<std::string> queue_;
    std::vector<int> order_;  // queue indices in play order
    int orderPos_ = -1;
    QueueSource source_ = QueueSource::None;
    std::string sourceId_;  // album/playlist id when source is one of those
    bool shuffle_ = false;
    RepeatMode repeat_ = RepeatMode::Off;
    float volume_ = 0.8f;
};
