#pragma once

#include "radio76/media_backend.h"

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace radio76::testing {

class MockMediaBackend final : public MediaBackend {
public:
    static PlaybackSnapshot Offline() {
        PlaybackSnapshot snapshot;
        snapshot.spotifyRunning = false;
        snapshot.sessionAvailable = false;
        snapshot.state = PlaybackState::Unavailable;
        snapshot.capturedAt = std::chrono::system_clock::now();
        return snapshot;
    }

    static PlaybackSnapshot SpotifyIdle() {
        PlaybackSnapshot snapshot;
        snapshot.spotifyRunning = true;
        snapshot.sessionAvailable = false;
        snapshot.state = PlaybackState::Closed;
        snapshot.capturedAt = std::chrono::system_clock::now();
        return snapshot;
    }

    static PlaybackSnapshot Playing(std::string title, std::string artist, std::string album) {
        PlaybackSnapshot snapshot;
        snapshot.spotifyRunning = true;
        snapshot.sessionAvailable = true;
        snapshot.state = PlaybackState::Playing;
        snapshot.metadata.present = true;
        snapshot.metadata.title = std::move(title);
        snapshot.metadata.artist = std::move(artist);
        snapshot.metadata.album = std::move(album);
        snapshot.canPlay = true;
        snapshot.canPause = true;
        snapshot.canToggle = true;
        snapshot.canNext = true;
        snapshot.canPrevious = true;
        snapshot.canSeek = true;
        snapshot.durationMs = 215000;
        snapshot.positionMs = 42000;
        snapshot.sourceAppId = "Spotify.exe";
        snapshot.capturedAt = std::chrono::system_clock::now();
        return snapshot;
    }

    static PlaybackSnapshot Paused(std::string title, std::string artist) {
        PlaybackSnapshot snapshot = Playing(std::move(title), std::move(artist), std::string{});
        snapshot.state = PlaybackState::Paused;
        return snapshot;
    }

    MockMediaBackend() {
        snapshot_.sourceAppId = "MockMediaBackend";
        nextPoll_ = snapshot_;
    }

    void SetSnapshot(PlaybackSnapshot snapshot) {
        std::lock_guard lock(mutex_);
        snapshot_ = std::move(snapshot);
        nextPoll_ = snapshot_;
    }

    void FailNextExecute() {
        std::lock_guard lock(mutex_);
        failNextExecute_ = true;
    }

    void SetExecuteDelay(std::chrono::milliseconds delay) {
        std::lock_guard lock(mutex_);
        executeDelay_ = delay;
    }

    void SetPollDelay(std::chrono::milliseconds delay) {
        std::lock_guard lock(mutex_);
        pollDelay_ = delay;
    }

    std::vector<TransportCommand> Commands() const {
        std::lock_guard lock(mutex_);
        return commands_;
    }

    std::size_t CommandCount() const {
        std::lock_guard lock(mutex_);
        return commands_.size();
    }

    PlaybackSnapshot Poll() override {
        std::chrono::milliseconds delay{0};
        PlaybackSnapshot result;
        {
            std::lock_guard lock(mutex_);
            delay = pollDelay_;
            snapshot_ = nextPoll_;
            result = snapshot_;
        }
        if (delay.count() > 0) {
            std::this_thread::sleep_for(delay);
        }
        return result;
    }

    bool Execute(const TransportCommand& command) override {
        std::chrono::milliseconds delay{0};
        {
            std::lock_guard lock(mutex_);
            if (failNextExecute_) {
                failNextExecute_ = false;
                return false;
            }
            delay = executeDelay_;
            commands_.push_back(command);
            switch (command.action) {
            case TransportAction::Stream:
                snapshot_.streamActive = true;
                snapshot_.state = PlaybackState::Playing;
                snapshot_.metadata = NormalizeMetadata(command.streamUrl, "Internet Stream", "Live");
                break;
            case TransportAction::StopStream:
                snapshot_.streamActive = false;
                snapshot_.state = PlaybackState::Stopped;
                break;
            case TransportAction::Play:
            case TransportAction::Pause:
            case TransportAction::Toggle:
            case TransportAction::Next:
            case TransportAction::Previous:
            case TransportAction::Seek:
                break;
            }
            nextPoll_ = snapshot_;
        }
        if (delay.count() > 0) {
            std::this_thread::sleep_for(delay);
        }
        return true;
    }

    void Shutdown() noexcept override {
        std::lock_guard lock(mutex_);
        commands_.clear();
    }

private:
    mutable std::mutex mutex_;
    PlaybackSnapshot snapshot_ = Playing("Mock Track", "Mock Artist", "Mock Album");
    PlaybackSnapshot nextPoll_ = snapshot_;
    std::vector<TransportCommand> commands_;
    bool failNextExecute_ = false;
    std::chrono::milliseconds executeDelay_{0};
    std::chrono::milliseconds pollDelay_{0};
};

}
