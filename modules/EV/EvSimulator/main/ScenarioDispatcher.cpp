// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "ScenarioDispatcher.hpp"

#include "Events.hpp"
#include "FsmContext.hpp"

#include <everest/logging.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <utility>

namespace module {

namespace {

namespace api = everest::lib::API::V1_0::types::ev_simulator;

Event make_plug_event() {
    return Event{PlugCmd{}};
}

Event make_configure_session_event(api::SessionConfigParams params) {
    return Event{std::move(params)};
}

Event make_stop_session_event() {
    return Event{StopSessionCmd{}};
}

Event make_pause_session_event() {
    return Event{PauseSessionCmd{}};
}

Event make_resume_session_event() {
    return Event{ResumeSessionCmd{}};
}

Event make_unplug_event() {
    return Event{UnplugCmd{}};
}

Event make_inject_fault_event(api::InjectFaultParams params) {
    return Event{std::move(params)};
}

Event make_clear_fault_event() {
    return Event{ClearFaultCmd{}};
}

// Phase tag for a timing override slot. Index into TimingResolver::consumed.
enum class Phase {
    PauseAt,
    ResumeAt,
    StopAfter,
    UnplugAfter,
    FaultAt,
    ClearFaultAt,
    Count // sentinel: number of real phases; sizes TimingResolver::consumed
};

// No to_string(ScenarioName) exists, and serialize(ScenarioName) returns a
// JSON-quoted string; this map yields the bare name for reject text.
const char* scenario_name_text(api::ScenarioName name) {
    switch (name) {
    case api::ScenarioName::AcIecBasic:
        return "AcIecBasic";
    case api::ScenarioName::AcIecPauseResume:
        return "AcIecPauseResume";
    case api::ScenarioName::AcIsoBasic:
        return "AcIsoBasic";
    case api::ScenarioName::AcIsoD20Basic:
        return "AcIsoD20Basic";
    case api::ScenarioName::DcIsoBasic:
        return "DcIsoBasic";
    case api::ScenarioName::DcIsoD20Basic:
        return "DcIsoD20Basic";
    case api::ScenarioName::DcIsoPauseResume:
        return "DcIsoPauseResume";
    case api::ScenarioName::DcIsoBpt:
        return "DcIsoBpt";
    case api::ScenarioName::DcIsoMcs:
        return "DcIsoMcs";
    case api::ScenarioName::DiodeFailSmoke:
        return "DiodeFailSmoke";
    case api::ScenarioName::AcIecRampUp:
        return "AcIecRampUp";
    case api::ScenarioName::DcIsoTaper:
        return "DcIsoTaper";
    }
    return "unknown";
}

const char* phase_field_name(Phase p) {
    switch (p) {
    case Phase::PauseAt:
        return "pause_at_ms";
    case Phase::ResumeAt:
        return "resume_at_ms";
    case Phase::StopAfter:
        return "stop_after_ms";
    case Phase::UnplugAfter:
        return "unplug_after_ms";
    case Phase::FaultAt:
        return "fault_at_ms";
    case Phase::ClearFaultAt:
        return "clear_fault_at_ms";
    case Phase::Count:
        break;
    }
    return "unknown";
}

std::optional<int32_t> phase_value(const api::ScenarioTimingOverrides& ov, Phase p) {
    switch (p) {
    case Phase::PauseAt:
        return ov.pause_at_ms;
    case Phase::ResumeAt:
        return ov.resume_at_ms;
    case Phase::StopAfter:
        return ov.stop_after_ms;
    case Phase::UnplugAfter:
        return ov.unplug_after_ms;
    case Phase::FaultAt:
        return ov.fault_at_ms;
    case Phase::ClearFaultAt:
        return ov.clear_fault_at_ms;
    case Phase::Count:
        break;
    }
    return std::nullopt;
}

// Resolves a phase offset to either its preset default or a supplied override,
// recording which phases a preset legitimately consumes so that an override
// targeting an unused phase can be rejected as not-applicable.
struct TimingResolver {
    const std::optional<api::ScenarioTimingOverrides>& ov;
    std::array<bool, static_cast<std::size_t>(Phase::Count)> consumed{}; // indexed by Phase
    std::optional<std::string> error;                                    // first reject reason

    std::chrono::milliseconds at(Phase p, int default_ms) {
        consumed[static_cast<std::size_t>(p)] = true;
        std::optional<int32_t> v;
        if (ov) {
            v = phase_value(*ov, p);
        }
        if (!v) {
            return std::chrono::milliseconds(default_ms);
        }
        if (*v <= 0 && !error) {
            error = "scenario timing must be positive";
        }
        return std::chrono::milliseconds(*v);
    }
};

std::vector<ScenarioStep> build_scenario_steps(api::ScenarioName name,
                                               const std::optional<api::ScenarioTimingOverrides>& timing,
                                               std::string& out_reject_reason) {
    std::vector<ScenarioStep> steps;
    TimingResolver r{timing};
    switch (name) {
    case api::ScenarioName::AcIecBasic: {
        api::AcIecSessionParams p;
        p.charging_current_a = 16.0f;
        p.three_phases = true;
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_event(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
        steps.push_back({r.at(Phase::StopAfter, 30000), make_stop_session_event()});
        break;
    }
    case api::ScenarioName::AcIsoBasic: {
        api::AcIso2SessionParams p;
        p.charging_current_a = 16.0f;
        p.three_phases = true;
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_event(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
        steps.push_back({r.at(Phase::StopAfter, 60000), make_stop_session_event()});
        break;
    }
    case api::ScenarioName::DcIsoBasic: {
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_event(api::DcIso2SessionParams{})});
        steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
        break;
    }
    case api::ScenarioName::AcIecPauseResume: {
        api::AcIecSessionParams p;
        p.charging_current_a = 16.0f;
        p.three_phases = true;
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_event(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
        steps.push_back({r.at(Phase::PauseAt, 30000), make_pause_session_event()});
        steps.push_back({r.at(Phase::ResumeAt, 60000), make_resume_session_event()});
        steps.push_back({r.at(Phase::StopAfter, 120000), make_stop_session_event()});
        steps.push_back({r.at(Phase::UnplugAfter, 125000), make_unplug_event()});
        break;
    }
    case api::ScenarioName::DcIsoPauseResume: {
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_event(api::DcIso2SessionParams{})});
        steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
        steps.push_back({r.at(Phase::PauseAt, 45000), make_pause_session_event()});
        steps.push_back({r.at(Phase::ResumeAt, 75000), make_resume_session_event()});
        steps.push_back({r.at(Phase::StopAfter, 180000), make_stop_session_event()});
        steps.push_back({r.at(Phase::UnplugAfter, 185000), make_unplug_event()});
        break;
    }
    case api::ScenarioName::AcIsoD20Basic: {
        api::AcIsoD20SessionParams p;
        p.charging_current_a = 16.0f;
        p.three_phases = true;
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_event(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
        steps.push_back({r.at(Phase::StopAfter, 120000), make_stop_session_event()});
        steps.push_back({r.at(Phase::UnplugAfter, 125000), make_unplug_event()});
        break;
    }
    case api::ScenarioName::DcIsoD20Basic: {
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_event(api::DcIsoD20SessionParams{})});
        steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
        steps.push_back({r.at(Phase::StopAfter, 180000), make_stop_session_event()});
        steps.push_back({r.at(Phase::UnplugAfter, 185000), make_unplug_event()});
        break;
    }
    case api::ScenarioName::DcIsoBpt: {
        api::DcIsoD20SessionParams p;
        api::BptParams bpt;
        bpt.discharge_max_current_limit = 50.0f;
        bpt.discharge_max_power_limit = 11000.0f;
        bpt.discharge_target_current = 30.0f;
        bpt.discharge_minimal_soc = 20.0f;
        p.bpt = bpt;
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_event(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
        steps.push_back({r.at(Phase::StopAfter, 180000), make_stop_session_event()});
        steps.push_back({r.at(Phase::UnplugAfter, 185000), make_unplug_event()});
        break;
    }
    case api::ScenarioName::DcIsoMcs: {
        api::DcIsoD20SessionParams p;
        p.mcs_enabled = true;
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_event(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
        steps.push_back({r.at(Phase::StopAfter, 180000), make_stop_session_event()});
        steps.push_back({r.at(Phase::UnplugAfter, 185000), make_unplug_event()});
        break;
    }
    case api::ScenarioName::AcIecRampUp: {
        api::AcIecSessionParams p;
        p.charging_current_a = 0.0f;
        p.three_phases = true;
        api::ChargingCurve curve;
        curve.loop = false;
        {
            api::CurvePoint cp;
            cp.t_offset_ms = 2000;
            cp.current_a = 8.0f;
            cp.three_phases = true;
            cp.ramp_ms = 2000;
            curve.points.push_back(cp);
        }
        {
            api::CurvePoint cp;
            cp.t_offset_ms = 8000;
            cp.current_a = 16.0f;
            cp.three_phases = true;
            cp.ramp_ms = 2000;
            curve.points.push_back(cp);
        }
        {
            api::CurvePoint cp;
            cp.t_offset_ms = 20000;
            cp.current_a = 32.0f;
            cp.three_phases = true;
            cp.ramp_ms = 4000;
            curve.points.push_back(cp);
        }
        p.curve = std::move(curve);
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_event(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
        steps.push_back({r.at(Phase::StopAfter, 60000), make_stop_session_event()});
        steps.push_back({r.at(Phase::UnplugAfter, 65000), make_unplug_event()});
        break;
    }
    case api::ScenarioName::DcIsoTaper: {
        api::DcIso2SessionParams p;
        api::ChargingCurve curve;
        curve.loop = false;
        {
            api::CurvePoint cp;
            cp.t_offset_ms = 0;
            cp.current_a = 100.0f;
            cp.three_phases = false;
            curve.points.push_back(cp);
        }
        {
            api::CurvePoint cp;
            cp.t_offset_ms = 30000;
            cp.current_a = 50.0f;
            cp.three_phases = false;
            cp.ramp_ms = 10000;
            curve.points.push_back(cp);
        }
        {
            api::CurvePoint cp;
            cp.t_offset_ms = 60000;
            cp.current_a = 20.0f;
            cp.three_phases = false;
            cp.ramp_ms = 10000;
            curve.points.push_back(cp);
        }
        {
            api::CurvePoint cp;
            cp.t_offset_ms = 90000;
            cp.current_a = 5.0f;
            cp.three_phases = false;
            cp.ramp_ms = 5000;
            curve.points.push_back(cp);
        }
        p.curve = std::move(curve);
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_event(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
        steps.push_back({r.at(Phase::StopAfter, 120000), make_stop_session_event()});
        steps.push_back({r.at(Phase::UnplugAfter, 125000), make_unplug_event()});
        break;
    }
    case api::ScenarioName::DiodeFailSmoke: {
        api::AcIecSessionParams p;
        p.charging_current_a = 32.0f;
        p.three_phases = true;
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_event(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
        api::InjectFaultParams fault;
        fault.type = api::FaultType::DiodeFail;
        steps.push_back({r.at(Phase::FaultAt, 15000), make_inject_fault_event(std::move(fault))});
        steps.push_back({r.at(Phase::ClearFaultAt, 20000), make_clear_fault_event()});
        break;
    }
    default:
        // Defense in depth: -Werror=switch already rejects missing cases at
        // compile time, but an out-of-range enum value cast from the wire
        // would otherwise leave the dispatcher silently idle. Return empty so
        // the caller can surface a CommandAck rejection.
        return {};
    }

    // 1. A non-positive override surfaced during resolution.
    if (r.error) {
        out_reject_reason = *r.error;
        return {};
    }

    // 2. Any override field set on a phase this preset does not consume is
    //    inapplicable.
    if (timing) {
        for (std::size_t i = 0; i < r.consumed.size(); ++i) {
            const auto p = static_cast<Phase>(i);
            if (phase_value(*timing, p).has_value() && !r.consumed[i]) {
                out_reject_reason = std::string("timing override ") + phase_field_name(p) + " not applicable to " +
                                    scenario_name_text(name);
                return {};
            }
        }
    }

    // 3. Strict-monotonic check over the produced steps. This relies on the
    //    invariant that presets place all t>0 steps contiguously after the
    //    leading t=0 prelude; we skip the prelude to the first positive index
    //    k and validate strictly-increasing `at` from k onward.
    {
        std::size_t k = 0;
        while (k < steps.size() && steps[k].at <= std::chrono::milliseconds(0)) {
            ++k;
        }
        for (std::size_t i = k + 1; i < steps.size(); ++i) {
            if (steps[i].at <= steps[i - 1].at) {
                out_reject_reason = "scenario timing not monotonic";
                return {};
            }
        }
    }

    return steps;
}

} // namespace

void ScenarioDispatcher::reset() {
    steps_.clear();
    next_idx_ = 0;
    loop_start_idx_.reset();
    loop_end_idx_.reset();
}

void ScenarioDispatcher::try_rewind_loop() {
    if (!loop_start_idx_.has_value() || !loop_end_idx_.has_value()) {
        return;
    }
    if (next_idx_ < *loop_end_idx_) {
        return;
    }
    next_idx_ = *loop_start_idx_;
    // Rebase start_at_ so the first looped step's `at` offset is measured
    // fresh from `now - at_of_first`. arm_next then computes (at - elapsed)
    // correctly on the next loop iteration.
    start_at_ = std::chrono::steady_clock::now() - steps_[*loop_start_idx_].at;
}

bool ScenarioDispatcher::within_loop_body() const {
    return loop_start_idx_.has_value() && loop_end_idx_.has_value() && next_idx_ >= *loop_start_idx_ &&
           next_idx_ < *loop_end_idx_;
}

void ScenarioDispatcher::dispatch_current(FsmContext& ctx) {
    // Inside a marked loop the same step index will be revisited after a
    // rewind, so we must keep the stored Event intact and copy. The non-loop
    // path keeps the move to avoid an extra deep copy on the common case
    // (most variant alternatives currently hold nested vectors).
    if (within_loop_body()) {
        ctx.enqueue(steps_[next_idx_].ev);
    } else {
        ctx.enqueue(std::move(steps_[next_idx_].ev));
    }
    ++next_idx_;
}

void ScenarioDispatcher::arm_next(FsmContext& ctx) {
    // Flush any past-due steps inline, then arm a positive ms delay for the
    // next future step. A zero arm would disarm the underlying timerfd
    // (timerfd_settime treats it_value=0 as disarm), so we must never call
    // arm_scenario_timer with a non-positive delay.
    //
    // Forward-progress guard: a marked loop whose body is entirely past-due
    // (e.g. every step at offset 0) would spin here forever. Cap inline
    // iterations at a generous bound; if we hit it, arm a 1 ms safety delay
    // so the event loop gets to flush other events.
    const std::size_t max_iterations = steps_.size() + 128;
    std::size_t iterations = 0;
    while (next_idx_ < steps_.size()) {
        if (++iterations > max_iterations) {
            EVLOG_warning << "ScenarioDispatcher: marked loop has no forward progress; "
                             "arming a 1 ms safety delay to let the event loop breathe";
            ctx.arm_scenario_timer(std::chrono::milliseconds(1));
            return;
        }
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_at_);
        auto delay_ms = steps_[next_idx_].at - elapsed;
        if (delay_ms > std::chrono::milliseconds(0)) {
            ctx.arm_scenario_timer(delay_ms);
            return;
        }
        dispatch_current(ctx);
        try_rewind_loop();
    }
}

void ScenarioDispatcher::start(api::ScenarioName name, const std::optional<api::ScenarioTimingOverrides>& timing,
                               FsmContext& ctx) {
    reset();
    start_at_ = std::chrono::steady_clock::now();

    std::string reason;
    steps_ = build_scenario_steps(name, timing, reason);
    if (steps_.empty()) {
        ctx.publish_e2m_command_ack("run_scenario", reason.empty() ? "unknown scenario name" : reason);
        return;
    }
    arm_next(ctx);
}

void ScenarioDispatcher::append_steps(std::vector<ScenarioStep> steps, FsmContext& ctx) {
    if (steps.empty()) {
        return;
    }
    const bool was_idle = (next_idx_ >= steps_.size());
    if (was_idle) {
        // No active scenario: seed start_at_ now so the new steps' offsets
        // are measured from this moment.
        start_at_ = std::chrono::steady_clock::now();
    }
    for (auto& s : steps) {
        steps_.push_back(std::move(s));
    }
    arm_next(ctx);
}

void ScenarioDispatcher::mark_loop(std::size_t begin_idx, std::size_t end_idx) {
    if (begin_idx >= end_idx || end_idx > steps_.size() || begin_idx >= steps_.size()) {
        EVLOG_warning << "ScenarioDispatcher: invalid mark_loop range [" << begin_idx << ", " << end_idx
                      << ") for step_count=" << steps_.size() << "; ignoring";
        return;
    }
    loop_start_idx_ = begin_idx;
    loop_end_idx_ = end_idx;
}

void ScenarioDispatcher::on_timer_fire(FsmContext& ctx) {
    if (next_idx_ >= steps_.size()) {
        return;
    }
    dispatch_current(ctx);
    // Rewind here too: when loop_end_idx_ == steps_.size(), arm_next's
    // `while (next_idx_ < steps_.size())` exits before its inline rewind
    // triggers, so the wrap must happen here for the loop to continue.
    try_rewind_loop();
    arm_next(ctx);
}

} // namespace module
