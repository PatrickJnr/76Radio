#pragma once

#include "radio76/log_file.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace radio76 {

struct RuntimeSettings {
    bool loggingEnabled = true;

    std::size_t logMaxBytes = kDefaultLogMaxBytes;
    std::chrono::milliseconds mediaStartupDelay{4000};
    std::chrono::milliseconds mediaPollInterval{900};
    std::chrono::milliseconds commandPollInterval{150};

    bool showStatus = false;
    bool showArtStatus = false;
    bool showArtwork = true;

    bool keepNowPlaying = false;
};

inline constexpr std::size_t kMaximumSettingsBytes = 8192;

std::filesystem::path SettingsFile(const std::filesystem::path& gameRoot);

RuntimeSettings ParseSettings(std::string_view text);

RuntimeSettings LoadSettings(const std::filesystem::path& path);

std::string DescribeSettings(const RuntimeSettings& settings);

}
