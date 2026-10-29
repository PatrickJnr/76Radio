#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace radio76 {

enum class PlaybackState {
    Unavailable,
    Closed,
    Opened,
    Changing,
    Playing,
    Paused,
    Stopped
};

enum class TransportAction {
    Play,
    Pause,
    Toggle,
    Next,
    Previous,
    Seek,

    Stream,

    StopStream
};

enum class CommandStatus {
    Accepted,
    Rejected,
    TimedOut,
    Unavailable
};

struct TrackMetadata {
    std::string title;
    std::string artist;
    std::string album;
    bool present = false;
};

struct PlaybackSnapshot {
    bool spotifyRunning = false;
    bool sessionAvailable = false;
    bool backendError = false;
    PlaybackState state = PlaybackState::Unavailable;
    TrackMetadata metadata;
    bool canPlay = false;
    bool canPause = false;
    bool canToggle = false;
    bool canNext = false;
    bool canPrevious = false;
    bool canSeek = false;
    std::int64_t durationMs = 0;
    std::int64_t positionMs = 0;
    std::string sourceAppId;

    bool streamActive = false;

    std::string streamStation;
    std::chrono::system_clock::time_point capturedAt{};
};

struct TransportCommand {
    TransportAction action = TransportAction::Toggle;
    std::int64_t positionMs = 0;
    std::string streamUrl;
};

const char* PlaybackStateName(PlaybackState state) noexcept;

const char* TransportActionName(TransportAction action) noexcept;

bool ParseTransportAction(std::string_view text, TransportAction& action) noexcept;

bool SnapshotsEquivalent(const PlaybackSnapshot& left, const PlaybackSnapshot& right) noexcept;

TrackMetadata NormalizeMetadata(std::string title, std::string artist, std::string album);

}
