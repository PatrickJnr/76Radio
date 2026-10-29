#include "radio76/media_types.h"

#include <algorithm>
#include <cctype>

namespace radio76 {
namespace {

constexpr std::size_t kMetadataLimit = 256;

std::string SanitizeText(std::string value) {
    for (char& character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte == 0x7F) {
            character = ' ';
        }
    }
    const auto first = value.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(' ');
    value = value.substr(first, last - first + 1);
    if (value.size() > kMetadataLimit) {
        std::size_t cut = kMetadataLimit;
        while (cut > 0 && (static_cast<unsigned char>(value[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        value.resize(cut);
    }
    return value;
}

}

const char* PlaybackStateName(PlaybackState state) noexcept {
    switch (state) {
    case PlaybackState::Unavailable:
        return "unavailable";
    case PlaybackState::Closed:
        return "closed";
    case PlaybackState::Opened:
        return "opened";
    case PlaybackState::Changing:
        return "changing";
    case PlaybackState::Playing:
        return "playing";
    case PlaybackState::Paused:
        return "paused";
    case PlaybackState::Stopped:
        return "stopped";
    }
    return "unavailable";
}

const char* TransportActionName(TransportAction action) noexcept {
    switch (action) {
    case TransportAction::Play:
        return "play";
    case TransportAction::Pause:
        return "pause";
    case TransportAction::Toggle:
        return "toggle";
    case TransportAction::Next:
        return "next";
    case TransportAction::Previous:
        return "previous";
    case TransportAction::Seek:
        return "seek";
    case TransportAction::Stream:
        return "stream";
    case TransportAction::StopStream:
        return "streamstop";
    }
    return "toggle";
}

bool ParseTransportAction(std::string_view text, TransportAction& action) noexcept {
    if (text == "play") {
        action = TransportAction::Play;
        return true;
    }
    if (text == "pause") {
        action = TransportAction::Pause;
        return true;
    }
    if (text == "toggle") {
        action = TransportAction::Toggle;
        return true;
    }
    if (text == "next") {
        action = TransportAction::Next;
        return true;
    }
    if (text == "previous") {
        action = TransportAction::Previous;
        return true;
    }
    if (text == "seek") {
        action = TransportAction::Seek;
        return true;
    }
    if (text == "streamstop") {
        action = TransportAction::StopStream;
        return true;
    }
    return false;
}

bool SnapshotsEquivalent(const PlaybackSnapshot& left, const PlaybackSnapshot& right) noexcept {
    return left.spotifyRunning == right.spotifyRunning &&
        left.sessionAvailable == right.sessionAvailable &&
        left.backendError == right.backendError &&
        left.state == right.state &&
        left.metadata.present == right.metadata.present &&
        left.metadata.title == right.metadata.title &&
        left.metadata.artist == right.metadata.artist &&
        left.metadata.album == right.metadata.album &&
        left.canPlay == right.canPlay &&
        left.canPause == right.canPause &&
        left.canToggle == right.canToggle &&
        left.canNext == right.canNext &&
        left.canPrevious == right.canPrevious &&
        left.canSeek == right.canSeek &&
        left.durationMs == right.durationMs &&
        left.positionMs == right.positionMs &&
        left.sourceAppId == right.sourceAppId &&
        left.streamActive == right.streamActive &&
        left.streamStation == right.streamStation;
}

TrackMetadata NormalizeMetadata(std::string title, std::string artist, std::string album) {
    TrackMetadata metadata;
    metadata.title = SanitizeText(std::move(title));
    metadata.artist = SanitizeText(std::move(artist));
    metadata.album = SanitizeText(std::move(album));
    metadata.present = !metadata.title.empty();
    return metadata;
}

}
