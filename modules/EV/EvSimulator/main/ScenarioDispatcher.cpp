// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "ScenarioDispatcher.hpp"

#include "Events.hpp"
#include "FsmContext.hpp"

#include <chrono>
#include <utility>

namespace module {

namespace {

namespace api = everest::lib::API::V1_0::types::ev_simulator;

Event make_plug_event() {
    Event ev;
    ev.kind = EventKind::Plug;
    return ev;
}

Event make_start_session_event(api::StartSessionParams params) {
    Event ev;
    ev.kind = EventKind::StartSession;
    ev.payload = std::move(params);
    return ev;
}

Event make_stop_session_event() {
    Event ev;
    ev.kind = EventKind::StopSession;
    return ev;
}

Event make_pause_session_event() {
    Event ev;
    ev.kind = EventKind::PauseSession;
    return ev;
}

Event make_resume_session_event() {
    Event ev;
    ev.kind = EventKind::ResumeSession;
    return ev;
}

Event make_unplug_event() {
    Event ev;
    ev.kind = EventKind::Unplug;
    return ev;
}

Event make_inject_fault_event(api::InjectFaultParams params) {
    Event ev;
    ev.kind = EventKind::InjectFault;
    ev.payload = std::move(params);
    return ev;
}

Event make_clear_fault_event() {
    Event ev;
    ev.kind = EventKind::ClearFault;
    return ev;
}

std::vector<ScenarioStep> build_ac_iec_basic() {
    std::vector<ScenarioStep> steps;
    steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
    api::StartSessionParams p;
    p.mode = api::ChargeMode::AcIec;
    p.charging_current_a = 16.0f;
    p.three_phases = true;
    steps.push_back({std::chrono::milliseconds(0), make_start_session_event(std::move(p))});
    steps.push_back({std::chrono::milliseconds(30000), make_stop_session_event()});
    return steps;
}

std::vector<ScenarioStep> build_ac_iso_basic() {
    std::vector<ScenarioStep> steps;
    steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
    api::StartSessionParams p;
    p.mode = api::ChargeMode::AcIso2;
    p.charging_current_a = 16.0f;
    p.three_phases = true;
    steps.push_back({std::chrono::milliseconds(0), make_start_session_event(std::move(p))});
    steps.push_back({std::chrono::milliseconds(60000), make_stop_session_event()});
    return steps;
}

std::vector<ScenarioStep> build_dc_iso_basic() {
    std::vector<ScenarioStep> steps;
    steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
    api::StartSessionParams p;
    p.mode = api::ChargeMode::DcIso2;
    steps.push_back({std::chrono::milliseconds(0), make_start_session_event(std::move(p))});
    return steps;
}

std::vector<ScenarioStep> build_ac_iec_pause_resume() {
    std::vector<ScenarioStep> steps;
    steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
    api::StartSessionParams p;
    p.mode = api::ChargeMode::AcIec;
    p.charging_current_a = 16.0f;
    p.three_phases = true;
    steps.push_back({std::chrono::milliseconds(0), make_start_session_event(std::move(p))});
    steps.push_back({std::chrono::milliseconds(30000), make_pause_session_event()});
    steps.push_back({std::chrono::milliseconds(60000), make_resume_session_event()});
    steps.push_back({std::chrono::milliseconds(120000), make_stop_session_event()});
    steps.push_back({std::chrono::milliseconds(125000), make_unplug_event()});
    return steps;
}

std::vector<ScenarioStep> build_dc_iso_pause_resume() {
    std::vector<ScenarioStep> steps;
    steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
    api::StartSessionParams p;
    p.mode = api::ChargeMode::DcIso2;
    steps.push_back({std::chrono::milliseconds(0), make_start_session_event(std::move(p))});
    steps.push_back({std::chrono::milliseconds(45000), make_pause_session_event()});
    steps.push_back({std::chrono::milliseconds(75000), make_resume_session_event()});
    steps.push_back({std::chrono::milliseconds(180000), make_stop_session_event()});
    steps.push_back({std::chrono::milliseconds(185000), make_unplug_event()});
    return steps;
}

std::vector<ScenarioStep> build_ac_iso_d20_basic() {
    std::vector<ScenarioStep> steps;
    steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
    api::StartSessionParams p;
    p.mode = api::ChargeMode::AcIsoD20;
    p.charging_current_a = 16.0f;
    p.three_phases = true;
    steps.push_back({std::chrono::milliseconds(0), make_start_session_event(std::move(p))});
    steps.push_back({std::chrono::milliseconds(120000), make_stop_session_event()});
    steps.push_back({std::chrono::milliseconds(125000), make_unplug_event()});
    return steps;
}

std::vector<ScenarioStep> build_dc_iso_d20_basic() {
    std::vector<ScenarioStep> steps;
    steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
    api::StartSessionParams p;
    p.mode = api::ChargeMode::DcIsoD20;
    steps.push_back({std::chrono::milliseconds(0), make_start_session_event(std::move(p))});
    steps.push_back({std::chrono::milliseconds(180000), make_stop_session_event()});
    steps.push_back({std::chrono::milliseconds(185000), make_unplug_event()});
    return steps;
}

std::vector<ScenarioStep> build_dc_iso_bpt() {
    std::vector<ScenarioStep> steps;
    steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
    api::StartSessionParams p;
    p.mode = api::ChargeMode::DcIsoD20;
    api::BptParams bpt;
    bpt.discharge_max_current_limit = 50.0f;
    bpt.discharge_max_power_limit = 11000.0f;
    bpt.discharge_target_current = 30.0f;
    bpt.discharge_minimal_soc = 20.0f;
    p.bpt = bpt;
    steps.push_back({std::chrono::milliseconds(0), make_start_session_event(std::move(p))});
    steps.push_back({std::chrono::milliseconds(180000), make_stop_session_event()});
    steps.push_back({std::chrono::milliseconds(185000), make_unplug_event()});
    return steps;
}

std::vector<ScenarioStep> build_dc_iso_mcs() {
    std::vector<ScenarioStep> steps;
    steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
    api::StartSessionParams p;
    p.mode = api::ChargeMode::DcIsoD20;
    p.mcs = api::McsProfile{};
    steps.push_back({std::chrono::milliseconds(0), make_start_session_event(std::move(p))});
    steps.push_back({std::chrono::milliseconds(180000), make_stop_session_event()});
    steps.push_back({std::chrono::milliseconds(185000), make_unplug_event()});
    return steps;
}

std::vector<ScenarioStep> build_ac_iec_ramp_up() {
    std::vector<ScenarioStep> steps;
    steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
    api::StartSessionParams p;
    p.mode = api::ChargeMode::AcIec;
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
    steps.push_back({std::chrono::milliseconds(0), make_start_session_event(std::move(p))});
    steps.push_back({std::chrono::milliseconds(60000), make_stop_session_event()});
    steps.push_back({std::chrono::milliseconds(65000), make_unplug_event()});
    return steps;
}

std::vector<ScenarioStep> build_dc_iso_taper() {
    std::vector<ScenarioStep> steps;
    steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
    api::StartSessionParams p;
    p.mode = api::ChargeMode::DcIso2;
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
    steps.push_back({std::chrono::milliseconds(0), make_start_session_event(std::move(p))});
    steps.push_back({std::chrono::milliseconds(120000), make_stop_session_event()});
    steps.push_back({std::chrono::milliseconds(125000), make_unplug_event()});
    return steps;
}

std::vector<ScenarioStep> build_diode_fail_smoke() {
    std::vector<ScenarioStep> steps;
    steps.push_back({std::chrono::milliseconds(0), make_plug_event()});
    api::StartSessionParams p;
    p.mode = api::ChargeMode::AcIec;
    p.charging_current_a = 32.0f;
    p.three_phases = true;
    steps.push_back({std::chrono::milliseconds(0), make_start_session_event(std::move(p))});
    api::InjectFaultParams fault;
    fault.type = api::FaultType::DiodeFail;
    steps.push_back({std::chrono::milliseconds(15000), make_inject_fault_event(std::move(fault))});
    steps.push_back({std::chrono::milliseconds(20000), make_clear_fault_event()});
    return steps;
}

} // namespace

void ScenarioDispatcher::reset() {
    steps_.clear();
    next_idx_ = 0;
    loop_start_idx_.reset();
    loop_end_idx_.reset();
}

void ScenarioDispatcher::arm_next(FsmContext& ctx) {
    if (next_idx_ >= steps_.size()) {
        return;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_at_);
    auto delay_ms = steps_[next_idx_].at - elapsed;
    if (delay_ms < std::chrono::milliseconds(0)) {
        delay_ms = std::chrono::milliseconds(0);
    }
    // The runtime scenario timer is second-resolution; round up so a
    // sub-second delay still arms.
    auto delay_s = std::chrono::duration_cast<std::chrono::seconds>(delay_ms);
    if (delay_s < std::chrono::seconds(1) && delay_ms > std::chrono::milliseconds(0)) {
        delay_s = std::chrono::seconds(1);
    }
    ctx.arm_scenario_timer(delay_s);
}

void ScenarioDispatcher::start(api::ScenarioName name, FsmContext& ctx) {
    reset();
    start_at_ = std::chrono::steady_clock::now();

    switch (name) {
    case api::ScenarioName::AcIecBasic:
        steps_ = build_ac_iec_basic();
        break;
    case api::ScenarioName::AcIsoBasic:
        steps_ = build_ac_iso_basic();
        break;
    case api::ScenarioName::DcIsoBasic:
        steps_ = build_dc_iso_basic();
        break;
    case api::ScenarioName::AcIecPauseResume:
        steps_ = build_ac_iec_pause_resume();
        break;
    case api::ScenarioName::DcIsoPauseResume:
        steps_ = build_dc_iso_pause_resume();
        break;
    case api::ScenarioName::DiodeFailSmoke:
        steps_ = build_diode_fail_smoke();
        break;
    case api::ScenarioName::AcIsoD20Basic:
        steps_ = build_ac_iso_d20_basic();
        break;
    case api::ScenarioName::DcIsoD20Basic:
        steps_ = build_dc_iso_d20_basic();
        break;
    case api::ScenarioName::DcIsoBpt:
        steps_ = build_dc_iso_bpt();
        break;
    case api::ScenarioName::DcIsoMcs:
        steps_ = build_dc_iso_mcs();
        break;
    case api::ScenarioName::AcIecRampUp:
        steps_ = build_ac_iec_ramp_up();
        break;
    case api::ScenarioName::DcIsoTaper:
        steps_ = build_dc_iso_taper();
        break;
    }

    // Fire all steps whose offset has already elapsed (offset 0 at minimum).
    while (next_idx_ < steps_.size() && steps_[next_idx_].at <= std::chrono::milliseconds(0)) {
        ctx.enqueue(std::move(steps_[next_idx_].ev));
        ++next_idx_;
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
    // Fire any newly-appended steps whose offset is already in the past
    // (relative to start_at_).
    while (next_idx_ < steps_.size()) {
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_at_);
        if (steps_[next_idx_].at > elapsed) {
            break;
        }
        ctx.enqueue(std::move(steps_[next_idx_].ev));
        ++next_idx_;
    }
    arm_next(ctx);
}

void ScenarioDispatcher::mark_loop(std::size_t begin_idx, std::size_t end_idx) {
    loop_start_idx_ = begin_idx;
    loop_end_idx_ = end_idx;
}

void ScenarioDispatcher::on_timer_fire(FsmContext& ctx) {
    if (next_idx_ >= steps_.size()) {
        return;
    }
    ctx.enqueue(std::move(steps_[next_idx_].ev));
    ++next_idx_;
    // If we just consumed the last step of a marked loop range, rewind
    // next_idx_ to the loop's start and rebase start_at_ so the loop
    // segment's offset math stays consistent on the next pass.
    if (loop_start_idx_.has_value() && loop_end_idx_.has_value() && next_idx_ >= *loop_end_idx_) {
        next_idx_ = *loop_start_idx_;
        // Rebase start_at_ so the first looped step's `at` offset is
        // measured fresh from `now - at_of_first`. We use now - at[start]
        // so arm_next computes (at - elapsed) correctly on the first loop
        // iteration.
        start_at_ = std::chrono::steady_clock::now() - steps_[*loop_start_idx_].at;
    }
    arm_next(ctx);
}

} // namespace module
