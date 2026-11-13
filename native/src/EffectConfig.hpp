// SPDX-License-Identifier: MIT
#pragma once

#include "Effect.hpp"

#include <filesystem>

namespace WaterClick {
    std::filesystem::path effectConfigPath();
    const SPreset& loadEffect(const std::filesystem::path& path);
    bool saveEffect(const std::filesystem::path& path, const SPreset& preset);
}
