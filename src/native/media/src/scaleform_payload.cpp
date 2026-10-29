#include "radio76/scaleform_payload.h"

#include <charconv>
#include <chrono>
#include <cstdint>

namespace radio76 {
namespace {

void AppendField(std::string& output, std::string_view key, std::string_view value) {
    output.append(key);
    output.push_back('=');
    output.append(EscapePayloadValue(value));
    output.push_back('\n');
}

void AppendBool(std::string& output, std::string_view key, bool value) {
    AppendField(output, key, value ? "1" : "0");
}

void AppendInt(std::string& output, std::string_view key, std::int64_t value) {
    output.append(key);
    output.push_back('=');
    output.append(std::to_string(value));
    output.push_back('\n');
}

std::string_view Trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

}

std::string EscapePayloadValue(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\':
            output.append("\\\\");
            break;
        case '\n':
            output.append("\\n");
            break;
        case '\r':
            output.append("\\r");
            break;
        default:
            output.push_back(character);
            break;
        }
    }
    return output;
}

std::string UnescapePayloadValue(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (character != '\\' || index + 1 >= value.size()) {
            output.push_back(character);
            continue;
        }
        const char escape = value[++index];
        switch (escape) {
        case 'n':
            output.push_back('\n');
            break;
        case 'r':
            output.push_back('\r');
            break;
        case '\\':
            output.push_back('\\');
            break;
        default:
            output.push_back(escape);
            break;
        }
    }
    return output;
}

std::string SnapshotToPayload(const PlaybackSnapshot& snapshot) {
    std::string output;
    output.reserve(320);
    AppendField(output, "state", PlaybackStateName(snapshot.state));
    AppendBool(output, "spotify", snapshot.spotifyRunning);
    AppendBool(output, "session", snapshot.sessionAvailable);
    AppendBool(output, "error", snapshot.backendError);
    AppendBool(output, "metadata", snapshot.metadata.present);
    AppendField(output, "title", snapshot.metadata.title);
    AppendField(output, "artist", snapshot.metadata.artist);
    AppendField(output, "album", snapshot.metadata.album);
    AppendBool(output, "canPlay", snapshot.canPlay);
    AppendBool(output, "canPause", snapshot.canPause);
    AppendBool(output, "canToggle", snapshot.canToggle);
    AppendBool(output, "canNext", snapshot.canNext);
    AppendBool(output, "canPrevious", snapshot.canPrevious);
    AppendBool(output, "canSeek", snapshot.canSeek);
    AppendBool(output, "stream", snapshot.streamActive);
    AppendInt(output, "durationMs", snapshot.durationMs);
    AppendInt(output, "positionMs", snapshot.positionMs);
    AppendField(output, "source", snapshot.sourceAppId);
    const auto captured = std::chrono::duration_cast<std::chrono::milliseconds>(
        snapshot.capturedAt.time_since_epoch());
    AppendInt(output, "capturedMs", captured.count());
    return output;
}

std::optional<TransportCommand> ParseCommandPayload(std::string_view payload) {
    const auto text = Trim(payload);
    if (text.empty()) {
        return std::nullopt;
    }
    constexpr std::string_view seekPrefix = "seek:";
    if (text.substr(0, seekPrefix.size()) == seekPrefix) {
        const auto digits = text.substr(seekPrefix.size());
        if (digits.empty()) {
            return std::nullopt;
        }
        std::int64_t position = 0;
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), position);
        if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size() || position < 0) {
            return std::nullopt;
        }
        TransportCommand command;
        command.action = TransportAction::Seek;
        command.positionMs = position;
        return command;
    }
    TransportAction action = TransportAction::Toggle;
    if (!ParseTransportAction(text, action) || action == TransportAction::Seek ||
        action == TransportAction::Stream) {

        return std::nullopt;
    }
    TransportCommand command;
    command.action = action;
    return command;
}

}
