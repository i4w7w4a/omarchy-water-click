// SPDX-License-Identifier: MIT
// Derived from HyprRipple, Copyright (c) 2026 jgarza.
#include "EffectConfig.hpp"
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

#include <charconv>
#include <cmath>
#include <format>
#include <memory>
#include <sstream>
#include <stdexcept>

using namespace WaterClick;
using Clock = std::chrono::steady_clock;

namespace {
    constexpr char VERSION[] = WATERCLICK_VERSION;

    struct SWave {
        PHLMONITORREF monitor;
        Vector2D      center;
        Clock::time_point started;
        const SPreset* preset;
    };

    struct SState {
        HANDLE handle;
        CConfigValue<Config::INTEGER> animations{"animations:enabled"};
        std::filesystem::path configPath = effectConfigPath();
        const SPreset* selected = &loadEffect(configPath);
        std::optional<SWave> click, preview;
        SP<CEventLoopTimer> expire;
        CHyprSignalListener button, render, preRender, lock, reload, monitorRemoved;
#if WATERCLICK_HYPRLAND_SERIES == 57
        SP<IPC::Socket1::SCommand> command;
#else
        SP<SHyprCtlCommand> command;
#endif
        uint64_t clicks = 0;
        uint64_t previews = 0;
        uint64_t frames = 0;
    };

    std::unique_ptr<SState> state;

    bool locked() {
        return g_pSessionLockManager && g_pSessionLockManager->isSessionLocked();
    }

    bool allowed() {
        return *state->animations && !locked();
    }

    void damage(const SWave& wave, const PHLMONITOR& monitor) {
        if (monitor->m_dpmsStatus)
            monitor->addDamage(damageBox(wave.center, wave.preset->damageRadius).scale(monitor->m_scale).round());
    }

    void clearWave(std::optional<SWave>& wave) {
        if (!wave)
            return;
        if (const auto monitor = wave->monitor.lock())
            damage(*wave, monitor);
        wave.reset();
    }

    void scheduleExpiry() {
        std::optional<Clock::duration> timeout;
        bool extendedSampling = false;
        const auto now = Clock::now();
        for (const auto* wave : {&state->click, &state->preview}) {
            if (*wave) {
                const auto remaining = (*wave)->started + (*wave)->preset->duration - now;
                if (!timeout || remaining < *timeout)
                    timeout = remaining;
                extendedSampling |= (*wave)->preset != &DEFAULT_PRESET && samplesScene(*(*wave)->preset);
            }
        }
        if (!extendedSampling)
            releaseExtendedSample();
        state->expire->updateTimeout(timeout);
        if (!timeout) {
            state->render.reset();
            state->preRender.reset();
        }
    }

    void clearWaves() {
        clearWave(state->click);
        clearWave(state->preview);
        scheduleExpiry();
    }

    void expireWaves() {
        const auto now = Clock::now();
        for (auto* wave : {&state->click, &state->preview}) {
            if (*wave && now - (*wave)->started >= (*wave)->preset->duration)
                clearWave(*wave);
        }
        scheduleExpiry();
    }

    void queueWave(const std::optional<SWave>& wave, const PHLMONITOR& monitor, Render::CRenderPass& pass, Clock::time_point now) {
        if (!wave || wave->monitor != monitor)
            return;
        const auto age = now - wave->started;
        if (age >= wave->preset->duration)
            return;
        const float progress = std::chrono::duration<float>(age) / std::chrono::duration<float>(wave->preset->duration);
        pass.add(makeUnique<CWaterPass>(wave->center, progress, *wave->preset, state->frames));
        // The damage ring has already consumed this frame's damage.
        damage(*wave, monitor);
    }

    void queueWaves(const PHLMONITOR& monitor, Render::CRenderPass& pass) {
        if (!allowed())
            return;
        const auto now = Clock::now();
        queueWave(state->click, monitor, pass, now);
        queueWave(state->preview, monitor, pass, now);
    }

    void listenForRender() {
        if (state->render)
            return;
        auto& events = Event::bus()->m_events.render;
#if WATERCLICK_HYPRLAND_SERIES == 57
        state->render = events.stage.listen([](const Event::SRenderStageEvent& event) {
            if (event.stage == RENDER_LAST_MOMENT && event.context && !event.context->get().m_renderingSnapshot)
                queueWaves(event.monitor, event.context->get().m_pass);
        });
#else
        state->render = events.stage.listen([](eRenderStage stage) {
            if (stage != RENDER_LAST_MOMENT || g_pHyprRenderer->m_bRenderingSnapshot)
                return;
            if (const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock())
                queueWaves(monitor, g_pHyprRenderer->m_renderPass);
        });
#endif
        state->preRender = events.pre.listen([](const PHLMONITOR&) {
            if (!allowed())
                clearWaves();
        });
    }

    bool startWave(std::optional<SWave>& wave, const Vector2D& position, const SPreset& preset) {
        for (const auto& monitor : State::monitorState()->monitors()) {
            if (!monitor || monitor->isMirror() || !monitor->m_dpmsStatus || !CBox{monitor->m_position, monitor->m_size}.containsPoint(position))
                continue;

            clearWave(wave);
            wave = SWave{monitor, position - monitor->m_position, Clock::now(), &preset};
            scheduleExpiry();
            listenForRender();
            damage(*wave, monitor);
            return true;
        }
        return false;
    }

    void onButton(const IPointer::SButtonEvent& event, const Event::SCallbackInfo& info) {
        if (info.cancelled || event.button != BTN_LEFT || event.state != WL_POINTER_BUTTON_STATE_PRESSED || !allowed())
            return;

        if (startWave(state->click, g_pInputManager->getMouseCoordsInternal(), *state->selected))
            ++state->clicks;
    }

    std::string status() {
        return std::format(
            R"({{"plugin":"waterclick","version":"{}","style":"{}","radius":{},"duration_ms":{},"clicks":{},"previews":{},"frames":{},"active":{}}})",
            VERSION, state->selected->id, state->selected->radius, state->selected->duration.count(), state->clicks,
            state->previews, state->frames, state->click.has_value() || state->preview.has_value());
    }

    std::optional<double> parseCoordinate(std::string_view text) {
        double value = 0;
        const auto end = text.data() + text.size();
        const auto [parsed, error] = std::from_chars(text.data(), end, value);
        if (error != std::errc{} || parsed != end || !std::isfinite(value))
            return std::nullopt;
        return value;
    }

    std::string control(const std::string& request) {
        if (request.size() > 256)
            return "error: request too long";
        std::istringstream args{request};
        std::string name, action, style, x, y, extra;
        args >> name >> action >> style;
        if (name != "waterclick")
            return "unknown request";
        if ((action.empty() || action == "get") && style.empty())
            return status();
        if (action == "cursor-refresh" && style.empty()) {
#if WATERCLICK_HYPRLAND_SERIES == 56
            // 0.57 updates the current named cursor directly in setcursor.
            const auto cursorName = g_pHyprRenderer->m_lastCursorData.name;
            if (!cursorName.empty() && g_pHyprRenderer->shouldRenderCursor())
                g_pHyprRenderer->setCursorFromName(cursorName, true);
#endif
            return "ok";
        }
        if ((action != "set" && action != "preview") || style.empty())
            return "error: expected get, set <style> or preview <style> [x y]";
        const auto* preset = findPreset(style);
        if (!preset)
            return "error: unknown effect";

        if (action == "set") {
            if (args >> extra)
                return "error: expected set <style>";
            if (!saveEffect(state->configPath, *preset))
                return "error: could not save effect";
            state->selected = preset;
            return "ok";
        }

        auto position = g_pInputManager->getMouseCoordsInternal();
        if (args >> x) {
            if (!(args >> y) || args >> extra)
                return "error: invalid preview coordinates";
            const auto px = parseCoordinate(x), py = parseCoordinate(y);
            if (!px || !py)
                return "error: invalid preview coordinates";
            position = {*px, *py};
        }
        if (!*state->animations)
            return "error: animations disabled";
        if (locked())
            return "error: session locked";
        if (!startWave(state->preview, position, *preset))
            return "error: no active output at preview position";
        ++state->previews;
        return "ok";
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
    state->command = HyprlandAPI::registerHyprCtlCommand(handle, IPC::Socket1::SCommand{
        .name = "waterclick",
        .match = IPC::Socket1::COMMAND_MATCH_PREFIX,
        .handler = [](const IPC::Socket1::SRequest& request) { return IPC::Socket1::SResponse{control(request.command)}; },
    });
#else
    state->command = HyprlandAPI::registerHyprCtlCommand(handle, SHyprCtlCommand{
        .name = "waterclick",
        .exact = false,
        .fn = [](eHyprCtlOutputFormat, std::string request) { return control(request); },
    });
#endif

    state->expire = makeShared<CEventLoopTimer>(std::nullopt, [](SP<CEventLoopTimer>, void*) { expireWaves(); }, nullptr);
    g_pEventLoopManager->addTimer(state->expire);

    auto& events = Event::bus()->m_events;
    state->button = events.input.mouse.button.listen(onButton);
    state->reload = events.config.reloaded.listen([] {
        if (!allowed())
            clearWaves();
    });
    state->monitorRemoved = events.monitor.removed.listen([](const PHLMONITOR& monitor) {
        for (auto* wave : {&state->click, &state->preview}) {
            if (*wave && (*wave)->monitor == monitor)
                clearWave(*wave);
        }
        scheduleExpiry();
    });
    if (g_pSessionLockManager)
        state->lock = g_pSessionLockManager->m_events.lock.listen(clearWaves);
    return {"waterclick", "Selectable effects on left click", "i4w7w4a, jgarza", VERSION};
}

APICALL EXPORT void PLUGIN_EXIT() {
    clearWaves();
    state->expire->cancel();
    g_pEventLoopManager->removeTimer(state->expire);
    HyprlandAPI::unregisterHyprCtlCommand(state->handle, state->command);
#if WATERCLICK_HYPRLAND_SERIES == 56
    g_pHyprRenderer->m_renderPass.removeAllOfType(PASS_NAME);
#endif
    destroyShader();
    state.reset();
}
