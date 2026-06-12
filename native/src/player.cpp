#include "player.h"

#include <algorithm>
#include <numeric>
#include <random>

#include "library.h"

void Player::Shutdown() {
    UnloadCurrent();
}

void Player::UnloadCurrent() {
    if (loaded_) {
        StopMusicStream(music_);
        UnloadMusicStream(music_);
        loaded_ = false;
    }
    state_ = State::Stopped;
}

const Track* Player::Current() const {
    if (orderPos_ < 0 || orderPos_ >= static_cast<int>(order_.size())) return nullptr;
    return lib_.TrackById(queue_[order_[orderPos_]]);
}

float Player::TimePlayed() const { return loaded_ ? GetMusicTimePlayed(music_) : 0.0f; }
float Player::TimeLength() const { return loaded_ ? GetMusicTimeLength(music_) : 0.0f; }

void Player::BuildOrder(int firstQueueIndex) {
    order_.resize(queue_.size());
    std::iota(order_.begin(), order_.end(), 0);
    if (shuffle_) {
        static std::mt19937 rng{std::random_device{}()};
        std::shuffle(order_.begin(), order_.end(), rng);
        // Current track plays first
        auto it = std::find(order_.begin(), order_.end(), firstQueueIndex);
        if (it != order_.end()) std::iter_swap(order_.begin(), it);
        orderPos_ = 0;
    } else {
        orderPos_ = firstQueueIndex;
    }
}

void Player::PlayQueue(std::vector<std::string> trackIds, int startIndex, QueueSource source,
                       std::string sourceId) {
    if (trackIds.empty()) return;
    queue_ = std::move(trackIds);
    source_ = source;
    sourceId_ = std::move(sourceId);
    startIndex = std::clamp(startIndex, 0, static_cast<int>(queue_.size()) - 1);
    BuildOrder(startIndex);
    LoadCurrent(true);
}

const Track* Player::TrackAtOrderPos(int pos) const {
    if (pos < 0 || pos >= static_cast<int>(order_.size())) return nullptr;
    return lib_.TrackById(queue_[order_[pos]]);
}

void Player::JumpTo(int orderPos) {
    if (orderPos < 0 || orderPos >= static_cast<int>(order_.size())) return;
    orderPos_ = orderPos;
    LoadCurrent(true);
}

void Player::RemoveAt(int orderPos) {
    const int n = static_cast<int>(order_.size());
    if (orderPos < 0 || orderPos >= n) return;
    const bool wasCurrent = orderPos == orderPos_;
    const bool wasPlaying = state_ == State::Playing;

    const int qi = order_[orderPos];
    queue_.erase(queue_.begin() + qi);
    order_.erase(order_.begin() + orderPos);
    for (int& o : order_) {
        if (o > qi) o--;
    }
    if (queue_.empty()) {
        UnloadCurrent();
        orderPos_ = -1;
        return;
    }
    if (orderPos < orderPos_) {
        orderPos_--;
    } else if (wasCurrent) {
        // The next track slid into the removed slot; keep playing from there.
        if (orderPos_ >= static_cast<int>(order_.size())) {
            UnloadCurrent();
            orderPos_ = 0;
        } else {
            LoadCurrent(wasPlaying);
        }
    }
}

void Player::Append(const std::string& trackId) {
    if (queue_.empty()) return;
    queue_.push_back(trackId);
    order_.push_back(static_cast<int>(queue_.size()) - 1);
}

void Player::ClearQueue() {
    UnloadCurrent();
    queue_.clear();
    order_.clear();
    orderPos_ = -1;
    source_ = QueueSource::None;
    sourceId_.clear();
}

void Player::RemoveTrackId(const std::string& trackId) {
    for (int pos = 0; pos < static_cast<int>(order_.size()); pos++) {
        if (queue_[order_[pos]] == trackId) {
            RemoveAt(pos);
            return;
        }
    }
}

bool Player::LoadCurrent(bool autoplay) {
    UnloadCurrent();
    const Track* track = Current();
    if (track == nullptr || !IsAudioDeviceReady()) return false;
    music_ = LoadMusicStream(track->filePath.c_str());
    if (!IsMusicValid(music_)) return false;
    loaded_ = true;
    music_.looping = false;
    SetMusicVolume(music_, volume_);
    if (autoplay) {
        PlayMusicStream(music_);
        state_ = State::Playing;
    }
    return true;
}

void Player::TogglePause() {
    if (!loaded_) {
        // Restart the queue from the current position if we stopped at the end.
        if (!queue_.empty()) {
            if (orderPos_ < 0) orderPos_ = 0;
            LoadCurrent(true);
        }
        return;
    }
    if (state_ == State::Playing) {
        PauseMusicStream(music_);
        state_ = State::Paused;
    } else {
        if (state_ == State::Stopped) {
            PlayMusicStream(music_);
        } else {
            ResumeMusicStream(music_);
        }
        state_ = State::Playing;
    }
}

void Player::Prev() {
    if (loaded_ && GetMusicTimePlayed(music_) > 3.0f) {
        SeekMusicStream(music_, 0.0f);
        return;
    }
    Advance(-1, true);
}

void Player::Advance(int dir, bool manual) {
    if (queue_.empty()) return;
    const int n = static_cast<int>(order_.size());
    int next = orderPos_ + dir;
    if (next >= n) {
        if (repeat_ == RepeatMode::All || manual) {
            next = 0;
        } else {
            // End of queue: stop, keep position so play resumes from the top.
            UnloadCurrent();
            orderPos_ = 0;
            return;
        }
    } else if (next < 0) {
        next = manual ? n - 1 : 0;
    }
    orderPos_ = next;
    LoadCurrent(true);
}

void Player::SeekTo(float seconds) {
    if (!loaded_) return;
    SeekMusicStream(music_, std::clamp(seconds, 0.0f, TimeLength()));
}

void Player::SetVolume(float v) {
    volume_ = std::clamp(v, 0.0f, 1.0f);
    if (loaded_) SetMusicVolume(music_, volume_);
}

void Player::ToggleShuffle() {
    shuffle_ = !shuffle_;
    if (!queue_.empty() && orderPos_ >= 0 && orderPos_ < static_cast<int>(order_.size())) {
        BuildOrder(order_[orderPos_]);
    }
}

void Player::CycleRepeat() {
    repeat_ = static_cast<RepeatMode>((static_cast<int>(repeat_) + 1) % 3);
}

void Player::Update() {
    if (!loaded_) return;
    UpdateMusicStream(music_);
    if (state_ == State::Playing && !IsMusicStreamPlaying(music_)) {
        // Track finished
        if (repeat_ == RepeatMode::One) {
            SeekMusicStream(music_, 0.0f);
            PlayMusicStream(music_);
        } else {
            Advance(1, false);
        }
    }
}
