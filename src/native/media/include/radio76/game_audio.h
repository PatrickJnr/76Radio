#pragma once

#include <filesystem>
#include <optional>

namespace radio76 {

inline constexpr unsigned long kPipboyRadioCategoryId = 138006;

std::optional<double> ReadPipboyRadioVolume(const std::filesystem::path& prefsFile);

}
