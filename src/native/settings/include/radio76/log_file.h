#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace radio76 {

inline constexpr std::size_t kDefaultLogMaxBytes = 1024 * 1024;

std::filesystem::path LogArchivePath(const std::filesystem::path& logFile);

bool ResetLogFile(const std::filesystem::path& logFile);

bool AppendLogLine(
    const std::filesystem::path& logFile,
    std::string_view line,
    std::size_t maxBytes = kDefaultLogMaxBytes);

}
