#include "radio76/game_audio.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <string_view>

namespace radio76 {
namespace {

std::string LowerAscii(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char character : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

std::string_view TrimAscii(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<double> ParseNumber(const std::string& text) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

}

std::optional<double> ReadPipboyRadioVolume(const std::filesystem::path& prefsFile) {
    std::ifstream stream{prefsFile, std::ios::binary};
    if (!stream) {
        return std::nullopt;
    }
    std::map<std::string, std::string> entries;
    std::string section;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string_view text = TrimAscii(line);
        if (text.empty()) {
            continue;
        }
        if (text.front() == '[' && text.back() == ']') {
            section = LowerAscii(TrimAscii(text.substr(1, text.size() - 2)));
            continue;
        }
        if (section != "audiomenu") {
            continue;
        }
        const auto separator = text.find('=');
        if (separator == std::string_view::npos) {
            continue;
        }
        entries[LowerAscii(TrimAscii(text.substr(0, separator)))] = std::string{TrimAscii(text.substr(separator + 1))};
    }

    for (int index = 0; index < 32; ++index) {
        const auto identifier = entries.find("uid" + std::to_string(index));
        if (identifier == entries.end()) {
            continue;
        }
        const auto id = ParseNumber(identifier->second);
        if (!id || *id != static_cast<double>(kPipboyRadioCategoryId)) {
            continue;
        }
        const auto slider = entries.find("fval" + std::to_string(index));
        const auto sliderValue = slider == entries.end() ? std::nullopt : ParseNumber(slider->second);
        if (!sliderValue) {
            return std::nullopt;
        }
        double master = 1.0;
        const auto masterEntry = entries.find("faudiomastervolume");
        if (masterEntry != entries.end()) {
            master = ParseNumber(masterEntry->second).value_or(1.0);
        }
        return std::clamp(*sliderValue, 0.0, 1.0) * std::clamp(master, 0.0, 1.0);
    }
    return std::nullopt;
}

}
