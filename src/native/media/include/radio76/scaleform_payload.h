#pragma once

#include "radio76/media_types.h"

#include <optional>
#include <string>
#include <string_view>

namespace radio76 {

std::string EscapePayloadValue(std::string_view value);

std::string UnescapePayloadValue(std::string_view value);

std::string SnapshotToPayload(const PlaybackSnapshot& snapshot);

std::optional<TransportCommand> ParseCommandPayload(std::string_view payload);

}
