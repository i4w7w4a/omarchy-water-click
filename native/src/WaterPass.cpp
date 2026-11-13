// SPDX-License-Identifier: MIT
// Derived from HyprRipple, Copyright (c) 2026 jgarza.
#include "WaterPass.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include <algorithm>
#include <cmath>

using namespace WaterClick;
using namespace Render;

namespace {
    constexpr char VERTEX_SHADER[] = R"GLSL(#version 300 es
in vec2 pos;
uniform vec4 drawBox;
uniform vec2 frameSize;
void main() {
    vec2 pixel = drawBox.xy + pos * drawBox.zw;
    gl_Position = vec4(pixel / frameSize * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
    constexpr char WATER_SHADER[] =
#include "water.frag.inc"
        ;
    constexpr char EFFECTS_SHADER[] =
#include "effects.frag.inc"
        ;

    struct SUniforms {
        GLint drawBox, frameSize, scene, sampleOrigin, sampleSize, center, scale, radius, opacity, waveAmp, time, mode, fade;
    };

    struct SShader {
        SP<CShader> program;
        SUniforms   uniforms;
        bool        failed = false;
    };

    SShader               waterShader;
    SShader               effectsShader;
    SP<GL::CGLFramebuffer> shortSample, extendedSample;

    bool ensureShader(SShader& shader, const char* fragment) {
        if (shader.program || shader.failed)
            return bool(shader.program);

        auto program = makeShared<CShader>();
        if (!program->createProgram(VERTEX_SHADER, fragment, true)) {
            shader.failed = true;
            return false;
        }

        const GLuint id = program->program();
        shader.uniforms = {
            glGetUniformLocation(id, "drawBox"),
            glGetUniformLocation(id, "frameSize"),
            glGetUniformLocation(id, "scene"),
            glGetUniformLocation(id, "sampleOrigin"),
            glGetUniformLocation(id, "sampleSize"),
            glGetUniformLocation(id, "center"),
            glGetUniformLocation(id, "scale"),
            glGetUniformLocation(id, "radius"),
            glGetUniformLocation(id, "opacity"),
            glGetUniformLocation(id, "waveAmp"),
            glGetUniformLocation(id, "time"),
            glGetUniformLocation(id, "mode"),
            glGetUniformLocation(id, "fade"),
        };
        shader.program = std::move(program);
        return true;
    }

    bool copyScene(GL::CHyprOpenGLImpl& gl, GL::CGLFramebuffer& source, const CBox& box, int textureSize,
                   SP<GL::CGLFramebuffer>& sample, bool reuseLargerSample) {
#if WATERCLICK_HYPRLAND_SERIES == 57
        GL::CFramebufferBindingGuard bindings{GL::g_pHyprOpenGL};
        gl.setActiveTexture(GL_TEXTURE0);
#else
        GLint readFramebuffer = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        glActiveTexture(GL_TEXTURE0);
#endif
        if (!sample)
            sample = makeShared<GL::CGLFramebuffer>("waterclick-sample");
        const bool reusable = reuseLargerSample && sample->isAllocated() && sample->m_size.x >= textureSize && sample->m_drmFormat == source.m_drmFormat;
        const bool allocated = reusable || sample->alloc(textureSize, textureSize, source.m_drmFormat);
        if (allocated) {
            gl.setCapStatus(GL_SCISSOR_TEST, false);
#if WATERCLICK_HYPRLAND_SERIES == 57
            gl.bindFramebuffer(GL_READ_FRAMEBUFFER, source.getFBID());
            gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, sample->getFBID());
#else
            glBindFramebuffer(GL_READ_FRAMEBUFFER, source.getFBID());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sample->getFBID());
#endif
            glBlitFramebuffer(box.x, box.y, box.x + box.w, box.y + box.h,
                              0, 0, box.w, box.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
#if WATERCLICK_HYPRLAND_SERIES == 56
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, source.getFBID());
#endif
        return allocated;
    }

    struct SFrame {
        float radius, opacity, amplitude, fade;
    };

    SFrame effectFrame(const SPreset& preset, float progress) {
        const bool water = preset.effect == EEffect::WATER;
        const bool prism = preset.effect == EEffect::PRISM;
        const bool sparkle = preset.effect == EEffect::SPARKLE;
        const bool wave = water || prism;
        const float spread = std::min(progress / (wave ? 1.f : sparkle ? 0.75f : 0.7f), 1.f);
        const float remaining = 1.f - spread;
        const float radius = wave ? 0.04f + 0.96f * (1.f - remaining * (0.35f * remaining + 0.65f))
                                  : 1.f - (sparkle ? 0.8f : 0.92f) * remaining * remaining * remaining;
        const float fadeStart = water ? 0.2f : sparkle ? 0.45f : 0.32f;
        const float fade = std::clamp((progress - fadeStart) / (1.f - fadeStart), 0.f, 1.f);
        const float decay = std::clamp((progress - 0.3f) / 0.7f, 0.f, 1.f);
        const auto smoothstep = [](float value) { return value * value * (3.f - 2.f * value); };
        return {
            preset.radius * radius,
            preset.opacity * std::min(progress / (sparkle ? 0.04f : 0.06f), 1.f) * (1.f - smoothstep(fade)),
            preset.radius * 0.12f * preset.distortion * (water ? 0.8f : 1.f) * (1.f - smoothstep(decay)),
            fade,
        };
    }
}

CBox WaterClick::damageBox(const Vector2D& center, int radius) {
    const double diameter = radius * 2.0;
    return {center.x - radius, center.y - radius, diameter, diameter};
}

void WaterClick::releaseExtendedSample() {
    if (extendedSample)
        GL::g_pHyprOpenGL->makeEGLCurrent();
    extendedSample.reset();
}

void WaterClick::destroyShader() {
    if (waterShader.program || effectsShader.program || shortSample || extendedSample)
        GL::g_pHyprOpenGL->makeEGLCurrent();
    shortSample.reset();
    extendedSample.reset();
    waterShader = {};
    effectsShader = {};
}

CWaterPass::CWaterPass(Vector2D center, float progress, const SPreset& preset, uint64_t& frames) :
    m_center(center), m_progress(progress), m_preset(preset), m_frames(frames) {}

#if WATERCLICK_HYPRLAND_SERIES == 57
std::vector<UP<IPassElement>> CWaterPass::draw(CRenderContext& ctx) {
    const auto& renderData = ctx.m_data;
#else
std::vector<UP<IPassElement>> CWaterPass::draw() {
    const auto& renderData = g_pHyprRenderer->m_renderData;
#endif
    const auto monitor = renderData.pMonitor.lock();
    auto* source = dynamic_cast<GL::CGLFramebuffer*>(renderData.currentFB.get());
    if (!monitor || !source || source->m_size.x < 1 || source->m_size.y < 1)
        return {};

    const float scale = monitor->m_scale;
    auto center = m_center * scale;
    auto damage = renderData.damage.copy();
#if WATERCLICK_HYPRLAND_SERIES == 56
    // 0.56 composes into an output-oriented framebuffer; 0.57 rotates after composition.
    const auto transform = Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform));
    center = CBox{center, {}}.transform(transform, monitor->m_transformedSize.x, monitor->m_transformedSize.y).pos();
    damage.transform(transform, monitor->m_transformedSize.x, monitor->m_transformedSize.y);
#endif

    const float reach = m_preset.damageRadius * scale;
    const double x1 = std::max(0.0, std::floor(center.x - reach));
    const double y1 = std::max(0.0, std::floor(center.y - reach));
    const double x2 = std::min(source->m_size.x, std::ceil(center.x + reach));
    const double y2 = std::min(source->m_size.y, std::ceil(center.y + reach));
    const CBox box{x1, y1, x2 - x1, y2 - y1};
    auto& gl = *GL::g_pHyprOpenGL;
    damage.intersect(box);
    const bool water = m_preset.effect == EEffect::WATER;
    const bool sampling = samplesScene(m_preset);
    const bool extended = &m_preset != &DEFAULT_PRESET;
    auto& sample = extended ? extendedSample : shortSample;
    auto& shader = water ? waterShader : effectsShader;
    if (damage.empty() || !ensureShader(shader, water ? WATER_SHADER : EFFECTS_SHADER) ||
        (sampling && !copyScene(gl, *source, box, std::ceil(2 * reach) + 1, sample, extended)))
        return {};

    const auto frame = effectFrame(m_preset, m_progress);
    const auto& uniforms = shader.uniforms;
    const bool oldBlend = glIsEnabled(GL_BLEND);
    gl.blend(true);
    gl.useShader(shader.program);
    if (sampling) {
        sample->getTexture()->bind();
        glUniform1i(uniforms.scene, 0);
        glUniform2f(uniforms.sampleOrigin, box.x, box.y);
        glUniform2f(uniforms.sampleSize, box.w, box.h);
        glUniform1f(uniforms.waveAmp, frame.amplitude * scale);
    }
    glUniform4f(uniforms.drawBox, box.x, box.y, box.w, box.h);
    glUniform2f(uniforms.frameSize, source->m_size.x, source->m_size.y);
    glUniform2f(uniforms.center, center.x, center.y);
    glUniform1f(uniforms.scale, scale);
    glUniform1f(uniforms.radius, frame.radius * scale);
    glUniform1f(uniforms.opacity, frame.opacity);
    glUniform1f(uniforms.time, m_progress * std::chrono::duration<float>(m_preset.duration).count());
    if (!water) {
        glUniform1i(uniforms.mode, static_cast<int>(m_preset.effect));
        glUniform1f(uniforms.fade, frame.fade);
    }

    glBindVertexArray(shader.program->getUniformLocation(SHADER_SHADER_VAO));
    for (const auto& rect : damage.getRects()) {
#if WATERCLICK_HYPRLAND_SERIES == 57
        gl.scissor(ctx, &rect, false);
#else
        gl.scissor(&rect, false);
#endif
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    gl.setCapStatus(GL_SCISSOR_TEST, false);
    glBindVertexArray(0);
    if (sampling)
        sample->getTexture()->unbind();
    gl.blend(oldBlend);
    ++m_frames;
    return {};
}
