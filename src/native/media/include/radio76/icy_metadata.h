#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace radio76 {

class IcyMetadataReader {
public:
    explicit IcyMetadataReader(std::function<void(std::string_view)> trace = {});

    ~IcyMetadataReader();

    IcyMetadataReader(const IcyMetadataReader&) = delete;

    IcyMetadataReader& operator=(const IcyMetadataReader&) = delete;

    void Start(std::string_view url);

    void Stop() noexcept;

    [[nodiscard]] std::string Title() const;

    [[nodiscard]] std::string Station() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string ParseIcyStreamTitle(std::string_view block);

[[nodiscard]] std::string ParseIcyStationName(std::string_view header);

}
