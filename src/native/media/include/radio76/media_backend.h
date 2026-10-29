#pragma once

#include "radio76/media_types.h"

#include <filesystem>

namespace radio76 {

class MediaBackend {
public:
    virtual ~MediaBackend() = default;

    virtual PlaybackSnapshot Poll() = 0;

    virtual bool Execute(const TransportCommand& command) = 0;

    virtual void Shutdown() noexcept {}

    virtual void SetArtworkPath(const std::filesystem::path& path) {
        (void)path;
    }

    virtual void SetStationsPath(const std::filesystem::path& path) {
        (void)path;
    }
};

}
