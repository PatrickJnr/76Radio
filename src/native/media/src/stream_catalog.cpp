#include "radio76/stream_catalog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>
#include <utility>

namespace radio76 {
namespace {

constexpr std::size_t kMaximumStationsBytes = 256 * 1024;

std::string LowerAscii(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char character : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

bool IsSpace(char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

void SkipSpace(std::string_view text, std::size_t& cursor) {
    while (cursor < text.size() && IsSpace(text[cursor])) {
        ++cursor;
    }
}

bool ReadString(std::string_view text, std::size_t& cursor, std::string& value) {
    if (cursor >= text.size() || text[cursor] != '"') {
        return false;
    }
    ++cursor;
    value.clear();
    while (cursor < text.size()) {
        const char character = text[cursor++];
        if (character == '"') {
            return true;
        }
        if (character == '\\' && cursor < text.size()) {
            const char escaped = text[cursor++];
            switch (escaped) {
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            default:
                value.push_back(escaped);
                break;
            }
            continue;
        }
        value.push_back(character);
    }
    return false;
}

bool ReadPair(std::string_view text, std::size_t& cursor, std::string& key, std::string& value) {
    while (cursor < text.size()) {
        SkipSpace(text, cursor);
        if (cursor >= text.size() || text[cursor] == '}') {
            return false;
        }
        if (text[cursor] == ',') {
            ++cursor;
            continue;
        }
        if (!ReadString(text, cursor, key)) {
            return false;
        }
        SkipSpace(text, cursor);
        if (cursor >= text.size() || text[cursor] != ':') {
            return false;
        }
        ++cursor;
        SkipSpace(text, cursor);
        if (cursor >= text.size()) {
            return false;
        }
        if (text[cursor] == '"') {
            if (!ReadString(text, cursor, value)) {
                return false;
            }
            return true;
        }

        while (cursor < text.size() && text[cursor] != ',' && text[cursor] != '}') {
            ++cursor;
        }
        value.clear();
        return true;
    }
    return false;
}

std::size_t FindKeyInsensitive(std::string_view text, std::string_view needle, std::size_t from) {
    if (needle.empty() || needle.size() > text.size()) {
        return std::string_view::npos;
    }
    for (std::size_t index = from; index + needle.size() <= text.size(); ++index) {
        bool match = true;
        for (std::size_t offset = 0; offset < needle.size(); ++offset) {
            if (std::tolower(static_cast<unsigned char>(text[index + offset])) !=
                std::tolower(static_cast<unsigned char>(needle[offset]))) {
                match = false;
                break;
            }
        }
        if (match) {
            return index;
        }
    }
    return std::string_view::npos;
}

}

std::vector<StreamStation> ParseStationsDocument(std::string_view document) {
    std::vector<StreamStation> stations;
    if (document.size() > kMaximumStationsBytes) {
        return stations;
    }

    std::size_t search = 0;
    while (search < document.size()) {
        const std::size_t streamKey = FindKeyInsensitive(document, "\"streamurl\"", search);
        const std::size_t plainKey = FindKeyInsensitive(document, "\"url\"", search);
        std::size_t key = std::string_view::npos;
        if (streamKey == std::string_view::npos) {
            key = plainKey;
        } else if (plainKey == std::string_view::npos) {
            key = streamKey;
        } else {
            key = streamKey < plainKey ? streamKey : plainKey;
        }
        if (key == std::string_view::npos) {
            break;
        }
        const std::size_t start = document.rfind('{', key);
        const std::size_t end = document.find('}', key);
        if (start == std::string_view::npos || end == std::string_view::npos || end <= start) {
            break;
        }
        std::size_t cursor = start + 1;
        StreamStation station;
        std::string field;
        std::string value;
        while (ReadPair(document.substr(0, end + 1), cursor, field, value)) {
            const std::string lowered = LowerAscii(field);
            if (lowered == "name") {
                station.name = value;
            } else if (lowered == "streamurl" || lowered == "url") {
                station.url = value;
            }
        }
        if (!station.url.empty()) {
            stations.push_back(std::move(station));
        }
        search = end + 1;
    }
    return stations;
}

std::string StationNameForUrl(const std::vector<StreamStation>& stations, std::string_view url) {
    for (const auto& station : stations) {
        if (station.url == url) {
            return station.name;
        }
    }
    for (const auto& station : stations) {
        if (station.url.size() == url.size() &&
            LowerAscii(station.url) == LowerAscii(url)) {
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
        host = host.substr(0, port);
    }
    constexpr std::string_view www = "www.";
    if (host.rfind(www, 0) == 0) {
        host = host.substr(www.size());
    }
    if (host.empty()) {
        return "Internet stream";
    }
    return host;
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
    std::string name = StationNameForUrl(stations_, url);
    if (!name.empty()) {
        return name;
    }
    return StreamTitleFromUrl(url);
}

void StationCatalog::Refresh() {
    if (path_.empty()) {
        return;
    }
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time(path_, error);
    if (error) {
        if (loaded_) {
            return;
        }

        loaded_ = true;
        stations_.clear();
        return;
    }
    if (loaded_ && stamp == stamp_) {
        return;
    }
    stamp_ = stamp;
    loaded_ = true;
    std::ifstream stream{path_, std::ios::binary};
    if (!stream) {
        stations_.clear();
        return;
    }
    std::string document((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    stations_ = ParseStationsDocument(document);
}

}
