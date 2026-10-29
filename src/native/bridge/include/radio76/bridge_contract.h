#pragma once

#include "radio76/media_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace radio76::bridge {

inline constexpr std::uint32_t kContractVersion = 1;
inline constexpr std::size_t kMaxCommandBytes = 64;

inline constexpr std::size_t kMaxStreamCommandBytes = 900;
inline constexpr std::size_t kMaxMetadataChars = 256;
inline constexpr std::size_t kMaxStateBytes = 4096;
inline constexpr std::size_t kMaxCommandDocumentBytes = 1024;
inline constexpr std::size_t kMaxStateDocumentBytes = 4096;
inline constexpr std::int64_t kMaxSeekMs = 86400000;

enum class OutcomeCode {
    Accepted,
    Empty,
    TooLong,
    Malformed,
    Unsupported,
    QueueFull,
    Unavailable
};

struct CommandOutcome {
    OutcomeCode code = OutcomeCode::Empty;
    TransportCommand command{};
    std::string detail;

    [[nodiscard]] bool accepted() const noexcept {
        return code == OutcomeCode::Accepted;
    }
};

const char* OutcomeCodeName(OutcomeCode code) noexcept;

const char* ActionName(TransportAction action) noexcept;

std::string ClampMetadata(std::string_view value);

CommandOutcome InterpretCommand(std::string_view text);

std::string BuildStatePayload(const PlaybackSnapshot* snapshot);

std::string BuildOfflinePayload();

std::string BuildStateDocument(const PlaybackSnapshot* snapshot);

std::string BuildStateSignature(const PlaybackSnapshot* snapshot);

struct CommandDocument {
    std::uint64_t stamp = 0;
    std::string command;
};

bool ParseCommandDocument(std::string_view document, CommandDocument& result);

}
