// SPDX-License-Identifier: MIT
// Derived from HyprRipple, Copyright (c) 2026 jgarza.
#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>

#include <chrono>
#include <cstdint>

namespace WaterClick {
    inline constexpr auto DURATION = std::chrono::milliseconds{250};
    inline constexpr int  RADIUS = 80;
    // Gaussian band, wobble, sample displacement and a texel margin.
    inline constexpr int  DAMAGE_RADIUS = RADIUS + 26;
    inline constexpr char PASS_NAME[] = "CWaterClickPass";

#if WATERCLICK_HYPRLAND_SERIES == 57
#define WATERCLICK_RENDER_CONTEXT Render::CRenderContext&
#else
#define WATERCLICK_RENDER_CONTEXT
#endif

    class CWaterPass : public IPassElement {
      public:
        CWaterPass(Vector2D center, float progress, uint64_t& frames);
        std::vector<UP<IPassElement>> draw(WATERCLICK_RENDER_CONTEXT) override;
        bool needsLiveBlur(WATERCLICK_RENDER_CONTEXT) override { return false; }
        bool needsPrecomputeBlur(WATERCLICK_RENDER_CONTEXT) override { return false; }
        bool disableSimplification(WATERCLICK_RENDER_CONTEXT) override { return true; }
        const char* passName() override { return PASS_NAME; }
        ePassElementType type() override { return EK_CUSTOM; }

      private:
        Vector2D  m_center;
        float     m_progress;
        uint64_t& m_frames;
    };

#undef WATERCLICK_RENDER_CONTEXT

    CBox damageBox(const Vector2D& center);
    void destroyShader();
}
