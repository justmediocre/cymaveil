#pragma once

#include <string>

// MPRIS (org.mpris.MediaPlayer2) D-Bus integration: exposes playback state
// and metadata to the desktop and accepts transport commands, which is also
// how media keys reach the app on modern desktops (the shell binds
// XF86Audio* to MPRIS calls).
//
// Threading model: a worker thread owns the private bus connection outright —
// it dispatches incoming method calls and emits change signals. The main
// thread only hands in state snapshots (Publish) and drains the command
// queue (PollRequest); the worker wakes it from idle event-waiting via
// glfwPostEmptyEvent. No libdbus call ever happens off the worker after
// startup.

// Snapshot of everything MPRIS exposes besides the live position.
struct MprisState {
    std::string status = "Stopped";  // Playing | Paused | Stopped
    std::string trackId;             // library hash id; empty when no current track
    std::string title;
    std::string artist;
    std::string album;
    std::string artUrl;   // file:// URL of the extracted cover, may be empty
    long long lengthUs = 0;
    bool shuffle = false;
    std::string loop = "None";  // None | Playlist | Track
    double volume = 1.0;
    bool hasTrack = false;  // a stream is loaded (pause/seek work)
    bool hasQueue = false;  // a queue exists (next/prev/restart work)

    bool operator==(const MprisState&) const = default;
};

enum class MprisCommand {
    Raise,
    Quit,
    Next,
    Previous,
    Pause,
    PlayPause,
    Stop,
    Play,
    SeekBy,       // value = offset seconds (may be negative)
    SetPosition,  // value = absolute seconds, str = track id it applies to
    SetVolume,    // value = 0..1 (clamped by the player)
    SetShuffle,   // value = 0/1
    SetLoop,      // str = None | Playlist | Track
};

struct MprisRequest {
    MprisCommand cmd{};
    double value = 0;
    std::string str;
};

// path -> file:// URL with minimal percent-encoding (helper for artUrl).
std::string MprisFileUrl(const std::string& path);

class Mpris {
public:
    ~Mpris();
    // Connects, claims org.mpris.MediaPlayer2.cymaveil, spawns the worker.
    // Failure (no session bus, no dbus support built in) is non-fatal.
    void Start();
    void Stop();
    bool Active() const { return impl_ != nullptr; }

    // Main thread, once per frame: diffs against the last snapshot and only
    // wakes the worker when something D-Bus-visible changed. dtSec is the
    // frame delta, used to tell a seek from normal playback progress.
    void Publish(const MprisState& s, double positionSec, double dtSec);
    bool PollRequest(MprisRequest* out);

private:
    struct Impl;
    Impl* impl_ = nullptr;

    // Main-thread bookkeeping for change/seek detection
    MprisState lastSent_;
    long long lastPosUs_ = 0;
    bool everSent_ = false;
};
