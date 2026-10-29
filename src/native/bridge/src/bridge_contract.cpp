#include "radio76/bridge_contract.h"

#include "radio76/scaleform_payload.h"

namespace radio76::bridge {
namespace {

std::string JsonString(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const char character : value) {
        const unsigned char code = static_cast<unsigned char>(character);
        if (code < 0x20 || code == 0x7f) {
            result.push_back(' ');
            continue;
        }
        if (character == '"' || character == '\\') {
            result.push_back('\'');
            continue;
        }
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

std::string JsonFlag(bool value) {
    return value ? std::string{"1"} : std::string{"0"};
}

bool FindNumberAfter(std::string_view document, std::string_view key, std::uint64_t& value) {
    const auto keyPosition = document.find(key);
    if (keyPosition == std::string_view::npos) {
        return false;
    }
    const auto colon = document.find(':', keyPosition + key.size());
    if (colon == std::string_view::npos) {
        return false;
    }
    std::size_t cursor = colon + 1;
    while (cursor < document.size() && (document[cursor] == ' ' || document[cursor] == '\t')) {
        cursor++;
    }
    std::uint64_t parsed = 0;
    std::size_t digits = 0;
    while (cursor < document.size() && document[cursor] >= '0' && document[cursor] <= '9') {
        parsed = (parsed * 10) + static_cast<std::uint64_t>(document[cursor] - '0');
        cursor++;
        digits++;
    }
    if (digits == 0) {
        return false;
    }
    value = parsed;
    return true;
}

bool FindStringAfter(std::string_view document, std::string_view key, std::string& value) {
    const auto keyPosition = document.find(key);
    if (keyPosition == std::string_view::npos) {
        return false;
    }
    const auto colon = document.find(':', keyPosition + key.size());
    if (colon == std::string_view::npos) {
        return false;
    }
    const auto open = document.find('"', colon + 1);
    if (open == std::string_view::npos) {
        return false;
    }
    const auto close = document.find('"', open + 1);
    if (close == std::string_view::npos) {
        return false;
    }
    value.assign(document.substr(open + 1, close - open - 1));
    return true;
}

std::string_view Trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool IsStreamAddress(std::string_view address) {
    constexpr std::string_view http = "http://";
    constexpr std::string_view https = "https://";
    std::string_view host;
    if (address.rfind(https, 0) == 0) {
        host = address.substr(https.size());
    } else if (address.rfind(http, 0) == 0) {
        host = address.substr(http.size());
    } else {
        return false;
    }
    const auto hostEnd = host.find_first_of("/?#");
    const std::string_view name = host.substr(0, hostEnd);
    if (name.empty() || name.find('@') != std::string_view::npos) {
        return false;
    }
    for (const char character : address) {
        const auto code = static_cast<unsigned char>(character);
        if (code <= 0x20 || code == 0x7F || character == '"' || character == '\\') {
            return false;
        }
    }
    return true;
}

}

const char* OutcomeCodeName(OutcomeCode code) noexcept {
    switch (code) {
    case OutcomeCode::Accepted:
        return "accepted";
    case OutcomeCode::Empty:
        return "empty";
    case OutcomeCode::TooLong:
        return "too_long";
    case OutcomeCode::Malformed:
        return "malformed";
    case OutcomeCode::Unsupported:
        return "unsupported";
    case OutcomeCode::QueueFull:
        return "queue_full";
    case OutcomeCode::Unavailable:
        return "unavailable";
    }
    return "unknown";
}

const char* ActionName(TransportAction action) noexcept {
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
    }
    return "unknown";
}

std::string ClampMetadata(std::string_view value) {
    if (value.size() <= kMaxMetadataChars) {
        return std::string{value};
    }
    return std::string{value.substr(0, kMaxMetadataChars)};
}

CommandOutcome InterpretCommand(std::string_view text) {
    CommandOutcome outcome;
    const auto trimmed = Trim(text);
    if (trimmed.empty()) {
        outcome.code = OutcomeCode::Empty;
        outcome.detail = "empty command";
        return outcome;
    }
    constexpr std::string_view streamPrefix = "stream|";
    if (trimmed.rfind(streamPrefix, 0) == 0) {
        if (trimmed.size() > kMaxStreamCommandBytes) {
            outcome.code = OutcomeCode::TooLong;
            outcome.detail = "stream address exceeds the accepted length";
            return outcome;
        }
        const std::string address{Trim(trimmed.substr(streamPrefix.size()))};
        if (!IsStreamAddress(address)) {
            outcome.code = OutcomeCode::Malformed;
            outcome.detail = "stream address must be an http or https address";
            return outcome;
        }
        outcome.code = OutcomeCode::Accepted;
        outcome.command.action = TransportAction::Stream;
        outcome.command.streamUrl = address;
        outcome.detail = "stream";
        return outcome;
    }
    if (trimmed.size() > kMaxCommandBytes) {
        outcome.code = OutcomeCode::TooLong;
        outcome.detail = "command exceeds the accepted length";
        return outcome;
    }
    const auto parsed = ParseCommandPayload(trimmed);
    if (!parsed.has_value()) {
        outcome.code = OutcomeCode::Malformed;
        outcome.detail = "unrecognised command";
        return outcome;
    }
    if (parsed->action == TransportAction::Seek &&
        (parsed->positionMs < 0 || parsed->positionMs > kMaxSeekMs)) {
        outcome.code = OutcomeCode::Malformed;
        outcome.detail = "seek position outside the accepted range";
        return outcome;
    }
    outcome.code = OutcomeCode::Accepted;
    outcome.command = *parsed;
    outcome.detail = ActionName(parsed->action);
    return outcome;
}

std::string BuildStatePayload(const PlaybackSnapshot* snapshot) {
    if (snapshot == nullptr) {
        return BuildOfflinePayload();
    }
    PlaybackSnapshot clamped = *snapshot;
    clamped.metadata.title = ClampMetadata(clamped.metadata.title);
    clamped.metadata.artist = ClampMetadata(clamped.metadata.artist);
    clamped.metadata.album = ClampMetadata(clamped.metadata.album);
    clamped.sourceAppId = ClampMetadata(clamped.sourceAppId);
    std::string payload = "ready=1\n";
    payload += SnapshotToPayload(clamped);
    if (payload.size() > kMaxStateBytes) {
        payload.resize(kMaxStateBytes);
    }
    return payload;
}

namespace {

std::string BuildStateDocumentBody(const PlaybackSnapshot* snapshot, bool includeVolatileFields) {
    if (snapshot == nullptr) {
        return "{\"ready\":0,\"state\":\"unavailable\",\"host\":\"76radio\"}";
    }
    PlaybackSnapshot clamped = *snapshot;
    clamped.metadata.title = ClampMetadata(clamped.metadata.title);
    clamped.metadata.artist = ClampMetadata(clamped.metadata.artist);
    clamped.metadata.album = ClampMetadata(clamped.metadata.album);
    clamped.sourceAppId = ClampMetadata(clamped.sourceAppId);
    clamped.streamStation = ClampMetadata(clamped.streamStation);
    const auto captured = std::chrono::duration_cast<std::chrono::milliseconds>(
        clamped.capturedAt.time_since_epoch()).count();
    std::string document = "{\"ready\":1";
    document += ",\"host\":\"76radio\"";
    document += ",\"state\":" + JsonString(PlaybackStateName(clamped.state));
    document += ",\"spotify\":" + JsonFlag(clamped.spotifyRunning);
    document += ",\"session\":" + JsonFlag(clamped.sessionAvailable);
    document += ",\"metadata\":" + JsonFlag(clamped.metadata.present);
    document += ",\"title\":" + JsonString(clamped.metadata.title);
    document += ",\"artist\":" + JsonString(clamped.metadata.artist);
    document += ",\"album\":" + JsonString(clamped.metadata.album);
    document += ",\"canPlay\":" + JsonFlag(clamped.canPlay);
    document += ",\"canPause\":" + JsonFlag(clamped.canPause);
    document += ",\"canToggle\":" + JsonFlag(clamped.canToggle);
    document += ",\"canNext\":" + JsonFlag(clamped.canNext);
    document += ",\"canPrevious\":" + JsonFlag(clamped.canPrevious);
    document += ",\"canSeek\":" + JsonFlag(clamped.canSeek);
    document += ",\"durationMs\":" + std::to_string(clamped.durationMs);
    if (includeVolatileFields) {
        document += ",\"positionMs\":" + std::to_string(clamped.positionMs);
    }
    document += ",\"source\":" + JsonString(clamped.sourceAppId);
    document += ",\"stream\":" + JsonFlag(clamped.streamActive);
    document += ",\"station\":" + JsonString(clamped.streamStation);
    document += ",\"error\":" + JsonFlag(clamped.backendError);
    if (includeVolatileFields) {
        document += ",\"capturedMs\":" + std::to_string(captured);
    }
    document += "}";
    return document;
}

}

std::string BuildStateDocument(const PlaybackSnapshot* snapshot) {
    return BuildStateDocumentBody(snapshot, true);
}

std::string BuildStateSignature(const PlaybackSnapshot* snapshot) {
    return BuildStateDocumentBody(snapshot, false);
}

bool ParseCommandDocument(std::string_view document, CommandDocument& result) {
    if (document.empty() || document.size() > kMaxCommandDocumentBytes) {
        return false;
    }
    CommandDocument parsed;
    if (!FindNumberAfter(document, "\"stamp\"", parsed.stamp)) {
        return false;
    }
    if (!FindStringAfter(document, "\"command\"", parsed.command)) {
        return false;
    }
    if (parsed.command.empty()) {
        return false;
    }
    result = parsed;
    return true;
}

std::string BuildOfflinePayload() {
    std::string payload = "ready=0\n";
    payload += "state=unavailable\n";
    payload += "spotify=0\n";
    payload += "session=0\n";
    payload += "metadata=0\n";
    payload += "title=\n";
    payload += "artist=\n";
    payload += "album=\n";
    payload += "canPlay=0\n";
    payload += "canPause=0\n";
    payload += "canToggle=0\n";
    payload += "canNext=0\n";
    payload += "canPrevious=0\n";
    payload += "canSeek=0\n";
    payload += "durationMs=0\n";
    payload += "positionMs=0\n";
    payload += "source=\n";
    payload += "error=0\n";
    payload += "capturedMs=0\n";
    return payload;
}

}
