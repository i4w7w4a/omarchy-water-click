// SPDX-License-Identifier: MIT
// Derived from HyprRipple, Copyright (c) 2026 jgarza.
#include "WaterPass.hpp"

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <linux/input-event-codes.h>

#include <format>
#include <memory>
#include <stdexcept>

using namespace WaterClick;
using Clock = std::chrono::steady_clock;

namespace {
    struct SWave {
        PHLMONITORREF monitor;
        Vector2D      center;
        Clock::time_point started;
    };

    struct SState {
        HANDLE handle;
        CConfigValue<Config::INTEGER> animations{"animations:enabled"};
        std::optional<SWave> wave;
        SP<CEventLoopTimer> expire;
        CHyprSignalListener button, render, preRender, lock, reload, monitorRemoved;
#if WATERCLICK_HYPRLAND_SERIES == 57
        SP<IPC::Socket1::SCommand> status;
#else
        SP<SHyprCtlCommand> status;
#endif
        uint64_t clicks = 0;
        uint64_t frames = 0;
    };

    std::unique_ptr<SState> state;

    bool allowed() {
        return *state->animations && (!g_pSessionLockManager || !g_pSessionLockManager->isSessionLocked());
    }

    void damage(const SWave& wave, const PHLMONITOR& monitor) {
        if (monitor->m_dpmsStatus)
            monitor->addDamage(damageBox(wave.center).scale(monitor->m_scale).round());
    }

    void clearWave() {
        if (!state->wave)
            return;
        if (const auto monitor = state->wave->monitor.lock())
            damage(*state->wave, monitor);
        state->wave.reset();
        state->expire->updateTimeout(std::nullopt);
        state->render.reset();
        state->preRender.reset();
    }

    void queueWave(const PHLMONITOR& monitor, Render::CRenderPass& pass) {
        if (!state->wave || state->wave->monitor != monitor || !allowed())
            return;
        const auto age = Clock::now() - state->wave->started;
        if (age >= DURATION)
            return;
        const float progress = std::chrono::duration<float>(age) / std::chrono::duration<float>(DURATION);
        pass.add(makeUnique<CWaterPass>(state->wave->center, progress, state->frames));
        // The damage ring has already consumed this frame's damage.
        damage(*state->wave, monitor);
    }

    void onButton(const IPointer::SButtonEvent& event, const Event::SCallbackInfo& info) {
        if (info.cancelled || event.button != BTN_LEFT || event.state != WL_POINTER_BUTTON_STATE_PRESSED || !allowed())
            return;

        const auto cursor = g_pInputManager->getMouseCoordsInternal();
        for (const auto& monitor : State::monitorState()->monitors()) {
            if (!monitor || monitor->isMirror() || !monitor->m_dpmsStatus || !CBox{monitor->m_position, monitor->m_size}.containsPoint(cursor))
                continue;

            if (state->wave) {
                if (const auto previous = state->wave->monitor.lock())
                    damage(*state->wave, previous);
            }

            state->wave = SWave{monitor, cursor - monitor->m_position, Clock::now()};
            ++state->clicks;
            state->expire->updateTimeout(DURATION);

            auto& events = Event::bus()->m_events.render;
            if (!state->render) {
#if WATERCLICK_HYPRLAND_SERIES == 57
                state->render = events.stage.listen([](const Event::SRenderStageEvent& event) {
                    if (event.stage == RENDER_LAST_MOMENT && event.context && !event.context->get().m_renderingSnapshot)
                        queueWave(event.monitor, event.context->get().m_pass);
                });
#else
                state->render = events.stage.listen([](eRenderStage stage) {
                    if (stage != RENDER_LAST_MOMENT || g_pHyprRenderer->m_bRenderingSnapshot)
                        return;
                    if (const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock())
                        queueWave(monitor, g_pHyprRenderer->m_renderPass);
                });
#endif
                state->preRender = events.pre.listen([](const PHLMONITOR& monitor) {
                    if (state->wave && state->wave->monitor == monitor && !allowed())
                        clearWave();
                });
            }
            damage(*state->wave, monitor);
            return;
        }
    }

    std::string status() {
        return std::format(
            R"({{"plugin":"waterclick","radius":{},"duration_ms":{},"clicks":{},"frames":{},"active":{}}})",
            RADIUS, DURATION.count(), state->clicks, state->frames, state->wave.has_value());
    }
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    if (std::string_view{__hyprland_api_get_hash()} != __hyprland_api_get_client_hash())
        throw std::runtime_error("waterclick was built for a different Hyprland ABI");

    state = std::make_unique<SState>();
    state->handle = handle;
#if WATERCLICK_HYPRLAND_SERIES == 57
    state->status = HyprlandAPI::registerHyprCtlCommand(handle, IPC::Socket1::SCommand{
        .name = "waterclick get",
        .handler = [](const IPC::Socket1::SRequest&) { return IPC::Socket1::SResponse{status()}; },
    });
#else
    state->status = HyprlandAPI::registerHyprCtlCommand(handle, SHyprCtlCommand{
        .name = "waterclick get",
        .fn = [](eHyprCtlOutputFormat, std::string) { return status(); },
    });
#endif

    state->expire = makeShared<CEventLoopTimer>(std::nullopt, [](SP<CEventLoopTimer>, void*) { clearWave(); }, nullptr);
    g_pEventLoopManager->addTimer(state->expire);

    auto& events = Event::bus()->m_events;
    state->button = events.input.mouse.button.listen(onButton);
    state->reload = events.config.reloaded.listen([] {
        if (!allowed())
            clearWave();
    });
    state->monitorRemoved = events.monitor.removed.listen([](const PHLMONITOR& monitor) {
        if (state->wave && state->wave->monitor == monitor)
            clearWave();
    });
    if (g_pSessionLockManager)
        state->lock = g_pSessionLockManager->m_events.lock.listen(clearWave);
    return {"waterclick", "One short water refraction ripple on left click", "i4w7w4a, jgarza", "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    clearWave();
    state->expire->cancel();
    g_pEventLoopManager->removeTimer(state->expire);
    HyprlandAPI::unregisterHyprCtlCommand(state->handle, state->status);
#if WATERCLICK_HYPRLAND_SERIES == 56
    g_pHyprRenderer->m_renderPass.removeAllOfType(PASS_NAME);
#endif
    destroyShader();
    state.reset();
}
