// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "WeldingDetectionVoltage.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace module {

namespace {
constexpr std::array<std::pair<WeldingDetectionVoltageSource, const char*>, 4> SOURCE_NAMES{{
    {WeldingDetectionVoltageSource::PowermeterCarSide, "powermeter_car_side"},
    {WeldingDetectionVoltageSource::OverVoltageMonitor, "over_voltage_monitor"},
    {WeldingDetectionVoltageSource::IsolationMonitor, "isolation_monitor"},
    {WeldingDetectionVoltageSource::PowerSupply, "power_supply"},
}};

bool is_connected(WeldingDetectionVoltageSource source, const WeldingDetectionVoltageSources& connected) {
    switch (source) {
    case WeldingDetectionVoltageSource::PowermeterCarSide:
        return connected.powermeter_car_side;
    case WeldingDetectionVoltageSource::OverVoltageMonitor:
        return connected.over_voltage_monitor;
    case WeldingDetectionVoltageSource::IsolationMonitor:
        return connected.isolation_monitor;
    case WeldingDetectionVoltageSource::PowerSupply:
        return true;
    }
    return false;
}

using Clock = std::chrono::steady_clock;

bool is_due(const std::optional<Clock::time_point>& deadline, Clock::time_point now) {
    return deadline.has_value() and now >= *deadline;
}
} // namespace

std::optional<WeldingDetectionVoltageSource> welding_detection_voltage_source_from_string(const std::string& s) {
    for (const auto& [source, name] : SOURCE_NAMES) {
        if (s == name) {
            return source;
        }
    }
    return std::nullopt;
}

std::string to_string(WeldingDetectionVoltageSource source) {
    for (const auto& [candidate, name] : SOURCE_NAMES) {
        if (candidate == source) {
            return name;
        }
    }
    return "unknown";
}

WeldingDetectionVoltageSource
resolve_welding_detection_voltage_source(WeldingDetectionVoltageSource configured,
                                         const WeldingDetectionVoltageSources& connected) {
    if (is_connected(configured, connected)) {
        return configured;
    }
    bool reached = false;
    for (const auto& [source, name] : SOURCE_NAMES) {
        reached = reached or source == configured;
        // Only an explicit choice selects the isolation monitor, which deviates from IEC 61851-23 CC.9 t103.
        if (reached and source != WeldingDetectionVoltageSource::IsolationMonitor and is_connected(source, connected)) {
            return source;
        }
    }
    return WeldingDetectionVoltageSource::PowerSupply;
}

WeldingDetectionVoltage::WeldingDetectionVoltage(WeldingDetectionVoltageSource source, PushCallback push,
                                                 StopSourceCallback stop_source, WarningCallback warning,
                                                 Timing timing) :
    m_source(source),
    m_push(std::move(push)),
    m_stop_source(std::move(stop_source)),
    m_warning(std::move(warning)),
    m_timing(timing),
    m_timer([this] { on_timer(); }) {
}

WeldingDetectionVoltage::WeldingDetectionVoltage(WeldingDetectionVoltageSource source, PushCallback push,
                                                 StopSourceCallback stop_source, WarningCallback warning) :
    WeldingDetectionVoltage(source, std::move(push), std::move(stop_source), std::move(warning), Timing{}) {
}

WeldingDetectionVoltage::~WeldingDetectionVoltage() = default;

WeldingDetectionVoltageSource WeldingDetectionVoltage::source() const {
    return m_source;
}

void WeldingDetectionVoltage::arm_timer(const State& state) {
    std::optional<Clock::time_point> next;
    for (const auto& deadline : {state.first_sample_deadline, state.watchdog_deadline, state.deferred_stop_deadline}) {
        if (deadline.has_value() and (not next.has_value() or *deadline < *next)) {
            next = deadline;
        }
    }
    if (not next.has_value()) {
        m_timer.stop();
        return;
    }
    m_timer.timeout(std::max(*next - Clock::now(), Clock::duration::zero()));
}

void WeldingDetectionVoltage::start() {
    auto state = m_state.handle();
    if (state->active) {
        return;
    }
    state->active = true;
    state->sample_pushed = false;
    state->slow_warned = false;
    state->last_sample.reset();
    // The stop now waits for the end of this phase.
    state->deferred_stop_deadline.reset();
    const auto now = Clock::now();
    state->first_sample_deadline = now + m_timing.first_sample_timeout;
    state->watchdog_deadline = now + m_timing.watchdog_timeout;
    arm_timer(*state);
}

void WeldingDetectionVoltage::finish() {
    bool stop_source = false;
    {
        auto state = m_state.handle();
        stop_source = state->stop_deferred;
        state->active = false;
        state->stop_deferred = false;
        state->cached_voltage_V.reset();
        state->first_sample_deadline.reset();
        state->watchdog_deadline.reset();
        state->deferred_stop_deadline.reset();
        arm_timer(*state);
    }

    if (stop_source and m_stop_source) {
        m_stop_source();
    }
}

void WeldingDetectionVoltage::defer_source_stop() {
    auto state = m_state.handle();
    state->stop_deferred = true;
    if (not state->active and not state->deferred_stop_deadline.has_value()) {
        state->deferred_stop_deadline = Clock::now() + m_timing.deferred_stop_timeout;
        arm_timer(*state);
    }
}

bool WeldingDetectionVoltage::is_active() const {
    return m_state.handle()->active;
}

bool WeldingDetectionVoltage::holds_source() const {
    auto state = m_state.handle();
    return state->active or state->stop_deferred;
}

void WeldingDetectionVoltage::update(WeldingDetectionVoltageSource source, float voltage_V) {
    if (source != m_source) {
        return;
    }

    std::scoped_lock push_lock(m_push_mutex);
    std::optional<std::chrono::milliseconds> slow_interval;
    {
        auto state = m_state.handle();
        state->cached_voltage_V = voltage_V;
        if (not state->active) {
            return;
        }
        const auto now = Clock::now();
        if (state->last_sample.has_value() and not state->slow_warned) {
            const auto interval = std::chrono::duration_cast<std::chrono::milliseconds>(now - *state->last_sample);
            if (interval > m_timing.slow_sample_interval) {
                state->slow_warned = true;
                slow_interval = interval;
            }
        }
        state->last_sample = now;
        state->sample_pushed = true;
        if (state->first_sample_deadline.has_value()) {
            state->first_sample_deadline.reset();
            arm_timer(*state);
        }
    }

    if (slow_interval.has_value()) {
        m_warning("Welding detection voltage source " + to_string(m_source) + " updates only every " +
                  std::to_string(slow_interval->count()) +
                  " ms; IEC 61851-23:2023 CC.6.3 expects at most 200 ms from measurement to communication");
    }
    m_push(voltage_V);
}

void WeldingDetectionVoltage::push_fallback() {
    std::scoped_lock push_lock(m_push_mutex);
    std::optional<float> cached;
    bool pushed = true;
    {
        auto state = m_state.handle();
        if (not is_due(state->first_sample_deadline, Clock::now())) {
            return;
        }
        state->first_sample_deadline.reset();
        cached = state->cached_voltage_V;
        pushed = state->sample_pushed;
    }
    if (pushed) {
        return;
    }
    if (cached.has_value()) {
        m_warning("No sample from welding detection voltage source " + to_string(m_source) + " within " +
                  std::to_string(m_timing.first_sample_timeout.count()) + " ms, reporting its last value " +
                  std::to_string(*cached) + " V");
        m_push(*cached);
    } else {
        m_warning("Welding detection voltage source " + to_string(m_source) +
                  " has reported no voltage yet; the EV gets the last present voltage");
    }
}

void WeldingDetectionVoltage::on_timer() {
    push_fallback();

    bool watchdog = false;
    bool stop_source = false;
    {
        auto state = m_state.handle();
        const auto now = Clock::now();
        if (is_due(state->watchdog_deadline, now)) {
            state->watchdog_deadline.reset();
            watchdog = true;
        }
        if (is_due(state->deferred_stop_deadline, now)) {
            state->deferred_stop_deadline.reset();
            if (state->stop_deferred and not state->active) {
                state->stop_deferred = false;
                stop_source = true;
            }
        }
        arm_timer(*state);
    }

    if (watchdog) {
        m_warning("Welding detection phase still running " + std::to_string(m_timing.watchdog_timeout.count()) +
                  " ms after it started, ending it");
        finish();
    }
    if (stop_source) {
        m_warning("No welding detection phase started within " +
                  std::to_string(m_timing.deferred_stop_timeout.count()) + " ms of the power path opening, stopping " +
                  to_string(m_source));
        if (m_stop_source) {
            m_stop_source();
        }
    }
}

} // namespace module
