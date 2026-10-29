#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace radio76 {

struct StreamStation {
    std::string name;
    std::string url;
};

std::vector<StreamStation> ParseStationsDocument(std::string_view document);

std::string StationNameForUrl(const std::vector<StreamStation>& stations, std::string_view url);

std::string StreamTitleFromUrl(std::string_view url);

class StationCatalog {
public:
    explicit StationCatalog(std::filesystem::path path = {});

    void SetPath(const std::filesystem::path& path);

    std::string TitleFor(std::string_view url);

private:
    void Refresh();

    std::filesystem::path path_;
    std::vector<StreamStation> stations_;
    std::filesystem::file_time_type stamp_{};
    bool loaded_ = false;
};

}
