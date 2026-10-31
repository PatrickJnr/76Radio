#include "radio76/stream_catalog.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>

namespace radio76 {
namespace {

constexpr std::size_t kMaximumStationsBytes = 256 * 1024;
constexpr std::size_t kMaximumStations = 256;
constexpr std::size_t kMaximumNameBytes = 256;
constexpr std::size_t kMaximumUrlBytes = 2048;
constexpr int kMaximumJsonDepth = 24;

std::string LowerAscii(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char character : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

bool IsHttpUrl(std::string_view value) {
    const std::string lowered = LowerAscii(value);
    return lowered.rfind("http://", 0) == 0 || lowered.rfind("https://", 0) == 0;
}

void AppendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint <= 0x7F) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text_(text) {}

    std::optional<std::vector<StreamStation>> ReadStations() {
        SkipSpace();
        if (!Consume('{')) {
            return std::nullopt;
        }
        bool foundStations = false;
        std::vector<StreamStation> stations;
        SkipSpace();
        if (!Consume('}')) {
            for (;;) {
                std::string key;
                if (!ReadString(key) || !ConsumeAfterSpace(':')) {
                    return std::nullopt;
                }
                if (key == "stations") {
                    if (foundStations || !ReadStationArray(stations)) {
                        return std::nullopt;
                    }
                    foundStations = true;
                } else if (!SkipValue(0)) {
                    return std::nullopt;
                }
                SkipSpace();
                if (Consume('}')) {
                    break;
                }
                if (!Consume(',')) {
                    return std::nullopt;
                }
            }
        }
        SkipSpace();
        if (!foundStations || cursor_ != text_.size()) {
            return std::nullopt;
        }
        return stations;
    }

private:
    void SkipSpace() {
        while (cursor_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[cursor_])) != 0) {
            ++cursor_;
        }
    }

    bool Consume(char expected) {
        if (cursor_ >= text_.size() || text_[cursor_] != expected) {
            return false;
        }
        ++cursor_;
        return true;
    }

    bool ConsumeAfterSpace(char expected) {
        SkipSpace();
        return Consume(expected);
    }

    bool ReadHex(std::uint32_t& value) {
        value = 0;
        for (int index = 0; index < 4; ++index) {
            if (cursor_ >= text_.size()) {
                return false;
            }
            const char character = text_[cursor_++];
            value <<= 4;
            if (character >= '0' && character <= '9') {
                value |= static_cast<std::uint32_t>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                value |= static_cast<std::uint32_t>(character - 'a' + 10);
            } else if (character >= 'A' && character <= 'F') {
                value |= static_cast<std::uint32_t>(character - 'A' + 10);
            } else {
                return false;
            }
        }
        return true;
    }

    bool ReadString(std::string& value) {
        SkipSpace();
        if (!Consume('"')) {
            return false;
        }
        value.clear();
        while (cursor_ < text_.size()) {
            const unsigned char character = static_cast<unsigned char>(text_[cursor_++]);
            if (character == '"') {
                return true;
            }
            if (character < 0x20) {
                return false;
            }
            if (character != '\\') {
                value.push_back(static_cast<char>(character));
                continue;
            }
            if (cursor_ >= text_.size()) {
                return false;
            }
            const char escape = text_[cursor_++];
            switch (escape) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u': {
                std::uint32_t first = 0;
                if (!ReadHex(first)) {
                    return false;
                }
                if (first >= 0xD800 && first <= 0xDBFF) {
                    if (cursor_ + 2 > text_.size() || text_[cursor_] != '\\' || text_[cursor_ + 1] != 'u') {
                        return false;
                    }
                    cursor_ += 2;
                    std::uint32_t second = 0;
                    if (!ReadHex(second) || second < 0xDC00 || second > 0xDFFF) {
                        return false;
                    }
                    first = 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
                } else if (first >= 0xDC00 && first <= 0xDFFF) {
                    return false;
                }
                AppendUtf8(value, first);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool SkipLiteral(std::string_view literal) {
        if (text_.substr(cursor_, literal.size()) != literal) {
            return false;
        }
        cursor_ += literal.size();
        return true;
    }

    bool SkipNumber() {
        const std::size_t start = cursor_;
        if (cursor_ < text_.size() && text_[cursor_] == '-') {
            ++cursor_;
        }
        if (cursor_ >= text_.size()) {
            return false;
        }
        if (text_[cursor_] == '0') {
            ++cursor_;
        } else {
            if (!std::isdigit(static_cast<unsigned char>(text_[cursor_]))) {
                return false;
            }
            while (cursor_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[cursor_]))) {
                ++cursor_;
            }
        }
        if (cursor_ < text_.size() && text_[cursor_] == '.') {
            ++cursor_;
            const std::size_t digits = cursor_;
            while (cursor_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[cursor_]))) {
                ++cursor_;
            }
            if (digits == cursor_) {
                return false;
            }
        }
        if (cursor_ < text_.size() && (text_[cursor_] == 'e' || text_[cursor_] == 'E')) {
            ++cursor_;
            if (cursor_ < text_.size() && (text_[cursor_] == '+' || text_[cursor_] == '-')) {
                ++cursor_;
            }
            const std::size_t digits = cursor_;
            while (cursor_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[cursor_]))) {
                ++cursor_;
            }
            if (digits == cursor_) {
                return false;
            }
        }
        return cursor_ > start;
    }

    bool SkipValue(int depth) {
        if (depth > kMaximumJsonDepth) {
            return false;
        }
        SkipSpace();
        if (cursor_ >= text_.size()) {
            return false;
        }
        if (text_[cursor_] == '"') {
            std::string ignored;
            return ReadString(ignored);
        }
        if (text_[cursor_] == '{') {
            ++cursor_;
            SkipSpace();
            if (Consume('}')) {
                return true;
            }
            for (;;) {
                std::string key;
                if (!ReadString(key) || !ConsumeAfterSpace(':') || !SkipValue(depth + 1)) {
                    return false;
                }
                SkipSpace();
                if (Consume('}')) {
                    return true;
                }
                if (!Consume(',')) {
                    return false;
                }
            }
        }
        if (text_[cursor_] == '[') {
            ++cursor_;
            SkipSpace();
            if (Consume(']')) {
                return true;
            }
            for (;;) {
                if (!SkipValue(depth + 1)) {
                    return false;
                }
                SkipSpace();
                if (Consume(']')) {
                    return true;
                }
                if (!Consume(',')) {
                    return false;
                }
            }
        }
        if (SkipLiteral("true") || SkipLiteral("false") || SkipLiteral("null")) {
            return true;
        }
        return SkipNumber();
    }

    bool ReadStation(StreamStation& station) {
        SkipSpace();
        if (!Consume('{')) {
            return SkipValue(0);
        }
        bool validTypes = true;
        SkipSpace();
        if (!Consume('}')) {
            for (;;) {
                std::string key;
                if (!ReadString(key) || !ConsumeAfterSpace(':')) {
                    return false;
                }
                if (key == "name" || key == "streamurl" || key == "url") {
                    std::string value;
                    if (!ReadString(value)) {
                        validTypes = false;
                        if (!SkipValue(0)) {
                            return false;
                        }
                    } else if (key == "name") {
                        station.name = std::move(value);
                    } else if (station.url.empty() || key == "streamurl") {
                        station.url = std::move(value);
                    }
                } else if (!SkipValue(0)) {
                    return false;
                }
                SkipSpace();
                if (Consume('}')) {
                    break;
                }
                if (!Consume(',')) {
                    return false;
                }
            }
        }
        if (!validTypes) {
            station = {};
        }
        return true;
    }

    bool ReadStationArray(std::vector<StreamStation>& stations) {
        SkipSpace();
        if (!Consume('[')) {
            return false;
        }
        SkipSpace();
        if (Consume(']')) {
            return true;
        }
        for (;;) {
            StreamStation station;
            if (!ReadStation(station)) {
                return false;
            }
            if (!station.name.empty() && !station.url.empty() &&
                station.name.size() <= kMaximumNameBytes &&
                station.url.size() <= kMaximumUrlBytes && IsHttpUrl(station.url)) {
                const std::string lowered = LowerAscii(station.url);
                const bool duplicate = std::any_of(
                    stations.begin(), stations.end(), [&lowered](const StreamStation& existing) {
                        return LowerAscii(existing.url) == lowered;
                    });
                if (!duplicate && stations.size() < kMaximumStations) {
                    stations.push_back(std::move(station));
                }
            }
            SkipSpace();
            if (Consume(']')) {
                return true;
            }
            if (!Consume(',')) {
                return false;
            }
        }
    }

    std::string_view text_;
    std::size_t cursor_ = 0;
};

std::optional<std::vector<StreamStation>> ParseDocument(std::string_view document) {
    if (document.empty() || document.size() > kMaximumStationsBytes) {
        return std::nullopt;
    }
    return JsonReader{document}.ReadStations();
}

}

std::vector<StreamStation> ParseStationsDocument(std::string_view document) {
    const auto parsed = ParseDocument(document);
    return parsed ? *parsed : std::vector<StreamStation>{};
}

std::string StationNameForUrl(const std::vector<StreamStation>& stations, std::string_view url) {
    for (const auto& station : stations) {
        if (station.url == url) {
            return station.name;
        }
    }
    const std::string lowered = LowerAscii(url);
    for (const auto& station : stations) {
        if (LowerAscii(station.url) == lowered) {
            return station.name;
        }
    }
    return {};
}

std::string StreamTitleFromUrl(std::string_view url) {
    const auto scheme = url.find("://");
    std::string_view rest = scheme == std::string_view::npos ? url : url.substr(scheme + 3);
    const auto authorityEnd = rest.find_first_of("/?#");
    std::string host{rest.substr(0, authorityEnd)};
    const auto at = host.rfind('@');
    if (at != std::string::npos) {
        host = host.substr(at + 1);
    }
    const auto port = host.find(':');
    if (port != std::string::npos) {
        host.resize(port);
    }
    if (host.rfind("www.", 0) == 0) {
        host.erase(0, 4);
    }
    return host.empty() ? "Internet stream" : host;
}

StationCatalog::StationCatalog(std::filesystem::path path) : path_(std::move(path)) {}

void StationCatalog::SetPath(const std::filesystem::path& path) {
    if (path_ == path) {
        return;
    }
    path_ = path;
    loaded_ = false;
    stations_.clear();
}

std::string StationCatalog::TitleFor(std::string_view url) {
    Refresh();
    const std::string configured = StationNameForUrl(stations_, url);
    return configured.empty() ? StreamTitleFromUrl(url) : configured;
}

std::string StationCatalog::ConfiguredTitleFor(std::string_view url) {
    Refresh();
    return StationNameForUrl(stations_, url);
}

void StationCatalog::Refresh() {
    if (path_.empty()) {
        return;
    }
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time(path_, error);
    if (error) {
        if (!loaded_) {
            loaded_ = true;
            stations_.clear();
        }
        return;
    }
    if (loaded_ && stamp == stamp_) {
        return;
    }
    std::ifstream stream{path_, std::ios::binary};
    if (!stream) {
        return;
    }
    std::string document((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto parsed = ParseDocument(document);
    if (!parsed) {
        return;
    }
    stamp_ = stamp;
    loaded_ = true;
    stations_ = *parsed;
}

}
