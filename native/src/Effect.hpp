// SPDX-License-Identifier: MIT
// Presets adapted from HyprRipple, Copyright (c) 2026 jgarza.
#pragma once

#include <array>
#include <chrono>
#include <string_view>

namespace WaterClick {
    enum class EEffect { CLASSIC = 0, RING = 1, SPARKLE = 2, NEGATIVE = 3, WATER = 4, PRISM = 5 };

    struct SPreset {
        std::string_view          id;
        EEffect                   effect;
        int                       radius;
        std::chrono::milliseconds duration;
        int                       damageRadius;
        float                     opacity;
        float                     distortion = 0.f;
    };

    // Bounds include the shader's feather/wobble and all displaced samples.
    // Short Water keeps its original 106 px reach and one localized scene copy.
    inline constexpr std::array PRESETS{
        SPreset{"water-short", EEffect::WATER, 80, std::chrono::milliseconds{250}, 106, 0.65f, 0.30f},
        SPreset{"classic", EEffect::CLASSIC, 110, std::chrono::milliseconds{450}, 116, 0.66f},
        SPreset{"ring", EEffect::RING, 120, std::chrono::milliseconds{500}, 124, 0.91f},
        SPreset{"sparkle", EEffect::SPARKLE, 145, std::chrono::milliseconds{650}, 156, 0.62f},
        SPreset{"negative", EEffect::NEGATIVE, 115, std::chrono::milliseconds{500}, 121, 0.96f},
        SPreset{"water", EEffect::WATER, 180, std::chrono::milliseconds{800}, 247, 0.92f, 0.85f},
        SPreset{"prism", EEffect::PRISM, 175, std::chrono::milliseconds{700}, 316, 0.91f, 0.85f},
    };
    inline constexpr const SPreset& DEFAULT_PRESET = PRESETS.front();

    inline bool samplesScene(const SPreset& preset) {
        return preset.effect == EEffect::WATER || preset.effect == EEffect::PRISM || preset.effect == EEffect::NEGATIVE;
    }

    inline const SPreset* findPreset(std::string_view id) {
        for (const auto& preset : PRESETS) {
            if (preset.id == id)
                return &preset;
        }
        return nullptr;
    }
}
