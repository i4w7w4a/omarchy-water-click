// SPDX-License-Identifier: MIT
#include "EffectConfig.hpp"

#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

fs::path WaterClick::effectConfigPath() {
    if (const char* config = std::getenv("XDG_CONFIG_HOME"); config && fs::path{config}.is_absolute())
        return fs::path{config} / "waterclick/style";
    if (const char* home = std::getenv("HOME"); home && fs::path{home}.is_absolute())
        return fs::path{home} / ".config/waterclick/style";
    return {};
}

const WaterClick::SPreset& WaterClick::loadEffect(const fs::path& path) {
    const int file = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (file < 0)
        return DEFAULT_PRESET;

    struct stat info{};
    char content[32];
    const bool regular = fstat(file, &info) == 0 && S_ISREG(info.st_mode);
    const auto size = regular ? read(file, content, sizeof(content)) : -1;
    close(file);
    if (size <= 0 || size >= static_cast<ssize_t>(sizeof(content)))
        return DEFAULT_PRESET;

    std::string_view id{content, static_cast<size_t>(size)};
    if (id.ends_with('\n'))
        id.remove_suffix(1);
    if (const auto* preset = findPreset(id))
        return *preset;
    return DEFAULT_PRESET;
}

bool WaterClick::saveEffect(const fs::path& path, const SPreset& preset) {
    if (path.empty())
        return false;

    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error)
        return false;

    const auto content = std::string{preset.id} + '\n';
    auto temporary = path.string() + ".XXXXXX";
    const int file = mkostemp(temporary.data(), O_CLOEXEC);
    if (file < 0)
        return false;

    const bool written = write(file, content.data(), content.size()) == static_cast<ssize_t>(content.size()) && fsync(file) == 0;
    const bool closed = close(file) == 0;
    if (written && closed && rename(temporary.c_str(), path.c_str()) == 0)
        return true;

    unlink(temporary.c_str());
    return false;
}
