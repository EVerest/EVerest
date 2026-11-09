// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>

#include <everest/timer.hpp>
#include <everest/util/async/monitor.hpp>

namespace module {

enum class WeldingDetectionVoltageSource {
    PowermeterCarSide,
    OverVoltageMonitor,
    IsolationMonitor,
    PowerSupply,
};

std::optional<WeldingDetectionVoltageSource> welding_detection_voltage_source_from_string(const std::string& s);
std::string to_string(WeldingDetectionVoltageSource source);

struct WeldingDetectionVoltageSources {
    bool powermeter_car_side{false};
    bool over_voltage_monitor{false};
    bool isolation_monitor{false};
};

/// \brief The configured source if connected, else the next connected one after it in the order of
/// WeldingDetectionVoltageSource, skipping the isolation monitor, which only an explicit choice selects. The power
/// supply is always available.
WeldingDetectionVoltageSource resolve_welding_detection_voltage_source(WeldingDetectionVoltageSource configured,
                                                                       const WeldingDetectionVoltageSources& connected);

/// \brief Reports the side B voltage of one selected source as EVSE present voltage during DC welding detection
/// (IEC 61851-23:2023 CC.7.6.17), from welding_detection Started until any end of the phase.
///
/// The over voltage monitor and the isolation monitor are normally stopped when the power path opens. If one of them
/// is the source, EvseManager defers that stop with defer_source_stop() and the stop callback runs when the phase
/// ends. Started travels on another MQTT topic than the power path events and may arrive after them, so a deferred
/// stop waits for the phase; if none starts within Timing::deferred_stop_timeout the stop runs anyway.
///
/// Every sample of the source is pushed while the phase runs; the ISO stack holds each WeldingDetectionRes until
/// two marked samples arrived after its request, so no sample is ever invented here.
///
/// Callbacks run without the internal lock held, from the calling thread or from the timer thread.
class WeldingDetectionVoltage {
public:
    using PushCallback = std::function<void(float voltage_V)>;
    using StopSourceCallback = std::function<void()>;
    using WarningCallback = std::function<void(const std::string&)>;

    struct Timing {
        // IEC 61851-23:2023 Table CC.9 t104 > t107: SessionStopReq is due 20 s after PowerDeliveryRes.
        std::chrono::milliseconds watchdog_timeout{20000};
        // Warn above this sample interval (CC.6.3: <= 200 ms from measurement to communication).
        std::chrono::milliseconds slow_sample_interval{1000};
        // A deferred stop whose phase never starts: Started follows the power path opening within the MQTT dispatch
        // latency, so a phase that has not started by then is not coming.
        std::chrono::milliseconds deferred_stop_timeout{2000};
    };

    WeldingDetectionVoltage(WeldingDetectionVoltageSource source, PushCallback push, StopSourceCallback stop_source,
                            WarningCallback warning, Timing timing);
    WeldingDetectionVoltage(WeldingDetectionVoltageSource source, PushCallback push, StopSourceCallback stop_source,
                            WarningCallback warning);
    ~WeldingDetectionVoltage();

    WeldingDetectionVoltage(const WeldingDetectionVoltage&) = delete;
    WeldingDetectionVoltage& operator=(const WeldingDetectionVoltage&) = delete;

    WeldingDetectionVoltageSource source() const;

    /// \brief welding_detection Started.
    void start();

    /// \brief Any end of the phase. Idempotent; also runs a deferred source stop.
    void finish();

    /// \brief The power path opened; the source's own stop is skipped and runs in finish(), or after
    /// Timing::deferred_stop_timeout if no phase starts.
    void defer_source_stop();

    bool is_active() const;

    /// \brief The phase runs or a source stop is deferred, so the source must keep measuring.
    bool holds_source() const;

    /// \brief Every voltage sample of every source.
    void update(WeldingDetectionVoltageSource source, float voltage_V);

private:
    using Clock = std::chrono::steady_clock;

    struct State {
        bool active{false};
        bool stop_deferred{false};
        bool slow_warned{false};
        std::optional<Clock::time_point> last_sample;
        std::optional<Clock::time_point> watchdog_deadline;
        std::optional<Clock::time_point> deferred_stop_deadline;
    };

    // Arms the timer for the nearest deadline, or stops it. Called with the state locked.
    void arm_timer(const State& state);
    void on_timer();

    const WeldingDetectionVoltageSource m_source;
    const PushCallback m_push;
    const StopSourceCallback m_stop_source;
    const WarningCallback m_warning;
    const Timing m_timing;

    mutable everest::lib::util::monitor<State> m_state;
    // Last member: destroyed first, so no timer callback outlives the state.
    Everest::SteadyTimer m_timer;
};

} // namespace module
