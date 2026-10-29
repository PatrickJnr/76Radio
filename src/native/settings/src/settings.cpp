#include "radio76/settings.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace radio76 {
namespace {

constexpr auto kMinimumMediaPollInterval = std::chrono::milliseconds(100);
constexpr auto kMaximumMediaPollInterval = std::chrono::milliseconds(60000);
constexpr auto kMinimumCommandPollInterval = std::chrono::milliseconds(20);
constexpr auto kMaximumCommandPollInterval = std::chrono::milliseconds(5000);
constexpr auto kMaximumStartupDelay = std::chrono::milliseconds(60000);

std::string_view Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::string Lower(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char character : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

std::string_view StripComment(std::string_view text) {
    const auto comment = text.find_first_of(";#");
    if (comment == std::string_view::npos) {
        return Trim(text);
    }
    return Trim(text.substr(0, comment));
}

bool ParseBoolean(std::string_view value, bool& on) {
    const std::string lowered = Lower(value);
    if (lowered == "1" || lowered == "true" || lowered == "on" || lowered == "yes") {
        on = true;
        return true;
    }
    if (lowered == "0" || lowered == "false" || lowered == "off" || lowered == "no") {
        on = false;
        return true;
    }
    return false;
}

bool ParseNumber(std::string_view value, long long& number) {
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

std::chrono::milliseconds ClampMilliseconds(
    long long value,
    std::chrono::milliseconds minimum,
    std::chrono::milliseconds maximum) {
    const auto clamped = std::clamp(value, minimum.count(), maximum.count());
    return std::chrono::milliseconds{clamped};
}

}

std::filesystem::path SettingsFile(const std::filesystem::path& gameRoot) {
    return gameRoot / L"Data" / L"XSCAL" / L"76Radio" / L"76Radio.ini";
}

RuntimeSettings ParseSettings(std::string_view text) {
    RuntimeSettings settings;
    std::string section;

    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const auto lineEnd = text.find_first_of("\r\n", cursor);
        const auto length = lineEnd == std::string_view::npos ? text.size() - cursor : lineEnd - cursor;
        std::string_view line = StripComment(text.substr(cursor, length));
        if (!line.empty()) {
            if (line.front() == '[' && line.back() == ']') {
                section = Lower(Trim(line.substr(1, line.size() - 2)));
            } else {
                const auto separator = line.find('=');
                if (separator != std::string_view::npos) {
                    const std::string key = Lower(Trim(line.substr(0, separator)));
                    const std::string_view value = Trim(line.substr(separator + 1));
                    if (section == "logging" && key == "enabled") {
                        bool on = settings.loggingEnabled;
                        if (ParseBoolean(value, on)) {
                            settings.loggingEnabled = on;
                        }
                    } else if (section == "logging" && key == "maxkb") {
                        long long number = 0;
                        if (ParseNumber(value, number)) {
                            if (number <= 0) {
                                settings.logMaxBytes = 0;
                            } else {
                                const auto clamped = std::clamp<long long>(number, 16, 65536);
                                settings.logMaxBytes = static_cast<std::size_t>(clamped) * 1024;
                            }
                        }
                    } else if (section == "media" && key == "startupdelayms") {
                        long long number = 0;
                        if (ParseNumber(value, number)) {
                            settings.mediaStartupDelay = ClampMilliseconds(
                                number, std::chrono::milliseconds{0}, kMaximumStartupDelay);
                        }
                    } else if (section == "media" && key == "pollintervalms") {
                        long long number = 0;
                        if (ParseNumber(value, number)) {
                            settings.mediaPollInterval = ClampMilliseconds(
                                number, kMinimumMediaPollInterval, kMaximumMediaPollInterval);
                        }
                    } else if (section == "channel" && key == "pollintervalms") {
                        long long number = 0;
                        if (ParseNumber(value, number)) {
                            settings.commandPollInterval = ClampMilliseconds(
                                number, kMinimumCommandPollInterval, kMaximumCommandPollInterval);
                        }
                    } else if (section == "ui" && key == "showstatus") {
                        bool on = settings.showStatus;
                        if (ParseBoolean(value, on)) {
                            settings.showStatus = on;
                        }
                    } else if (section == "ui" && key == "showartstatus") {
                        bool on = settings.showArtStatus;
                        if (ParseBoolean(value, on)) {
                            settings.showArtStatus = on;
                        }
                    } else if (section == "ui" && key == "showartwork") {
                        bool on = settings.showArtwork;
                        if (ParseBoolean(value, on)) {
                            settings.showArtwork = on;
                        }
                    } else if (section == "ui" && key == "keepnowplaying") {
                        bool on = settings.keepNowPlaying;
                        if (ParseBoolean(value, on)) {
                            settings.keepNowPlaying = on;
                        }
                    }
                }
            }
        }
        if (lineEnd == std::string_view::npos) {
            break;
        }
        cursor = lineEnd + 1;
        if (cursor < text.size() && text[cursor - 1] == '\r' && text[cursor] == '\n') {
            ++cursor;
        }
    }
    return settings;
}

RuntimeSettings LoadSettings(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaximumSettingsBytes) {
        return {};
    }
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return {};
    }
    std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (!stream.good() && !stream.eof()) {
        return {};
    }
    return ParseSettings(text);
}

std::string DescribeSettings(const RuntimeSettings& settings) {
    std::string line = "settings: logging ";
    line += settings.loggingEnabled ? "on" : "off";
    line += settings.logMaxBytes == 0
        ? std::string{", log unbounded"}
        : ", log limit " + std::to_string(settings.logMaxBytes / 1024) + " KB";
    line += ", startup " + std::to_string(settings.mediaStartupDelay.count()) + " ms";
    line += ", media poll " + std::to_string(settings.mediaPollInterval.count()) + " ms";
    line += ", command poll " + std::to_string(settings.commandPollInterval.count()) + " ms";
    line += ", ui status ";
    line += settings.showStatus ? "on" : "off";
    line += ", ui art status ";
    line += settings.showArtStatus ? "on" : "off";
    line += ", ui artwork ";
    line += settings.showArtwork ? "on" : "off";
    line += ", keep now playing ";
    line += settings.keepNowPlaying ? "on" : "off";
    return line;
}

}
