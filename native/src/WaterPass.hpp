// SPDX-License-Identifier: MIT
// Derived from HyprRipple, Copyright (c) 2026 jgarza.
#pragma once

#include "Effect.hpp"

#include <hyprland/src/render/pass/PassElement.hpp>

#include <cstdint>

namespace WaterClick {
    inline constexpr char PASS_NAME[] = "CWaterClickPass";

#if WATERCLICK_HYPRLAND_SERIES == 57
#define WATERCLICK_RENDER_CONTEXT Render::CRenderContext&
#else
#define WATERCLICK_RENDER_CONTEXT
#endif

    class CWaterPass : public IPassElement {
      public:
        CWaterPass(Vector2D center, float progress, const SPreset& preset, uint64_t& frames);
        std::vector<UP<IPassElement>> draw(WATERCLICK_RENDER_CONTEXT) override;
        bool needsLiveBlur(WATERCLICK_RENDER_CONTEXT) override { return false; }
        bool needsPrecomputeBlur(WATERCLICK_RENDER_CONTEXT) override { return false; }
        bool disableSimplification(WATERCLICK_RENDER_CONTEXT) override { return true; }
        const char* passName() override { return PASS_NAME; }
        ePassElementType type() override { return EK_CUSTOM; }

      private:
        Vector2D       m_center;
        float          m_progress;
        const SPreset& m_preset;
        uint64_t&      m_frames;
    };

#undef WATERCLICK_RENDER_CONTEXT

    CBox damageBox(const Vector2D& center, int radius);
    void releaseExtendedSample();
    void destroyShader();
}
