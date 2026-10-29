#pragma once

#include "radio76/media_backend.h"

#include <functional>
#include <memory>
#include <string_view>

namespace radio76 {

std::unique_ptr<MediaBackend> CreateWindowsMediaBackend(
    std::function<void(std::string_view)> trace = {});

}
