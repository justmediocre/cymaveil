#pragma once

#include <string>
#include <vector>

#include "raylib.h"

class Library;
struct Track;

enum class RepeatMode { Off, All, One };

// Playback engine on top of raylib's streaming Music API.
// Owns the queue; shuffle is an index indirection (order_) over queue_.
class Player {
public:
    explicit Player(Library& lib) : lib_(lib) {}

    void Shutdown();
    // Per-frame: feeds the stream and auto-advances when a track ends.
    void Update();

    void PlayQueue(std::vector<std::string> trackIds, int startIndex);
    void TogglePause();
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
    bool HasTrack() const { return loaded_; }
    const Track* Current() const;
    float TimePlayed() const;
    float TimeLength() const;

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
    bool shuffle_ = false;
    RepeatMode repeat_ = RepeatMode::Off;
    float volume_ = 0.8f;
};
