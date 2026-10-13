// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "ScenarioRunner.hpp"
#include <array>
#include <everest_api_types/ev_controller/codec.hpp>
#include <everest_api_types/ev_driver_sim/codec.hpp>
#include <utility>

namespace module {
namespace {

namespace api = everest::lib::API::V1_0::types::ev_controller;
namespace driver = everest::lib::API::V1_0::types::ev_driver_sim;
using Command = ScenarioRunner::Command;
using ScenarioStep = ScenarioRunner::Step;

Command make_plug_command() {
    return {"plug", "null"};
}

Command make_configure_session_command(api::SessionConfigParams params) {
    return {"configure_session", api::serialize(params)};
}

Command make_stop_session_command() {
    return {"stop_session", "null"};
}

Command make_pause_session_command() {
    return {"pause_session", "null"};
}

Command make_resume_session_command() {
    return {"resume_session", "null"};
}

Command make_unplug_command() {
    return {"unplug", "null"};
}

Command make_inject_fault_command(driver::InjectFaultParams params) {
    return {"inject_fault", driver::serialize(params)};
}

Command make_clear_fault_command() {
    return {"clear_fault", "null"};
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
const char* scenario_name_text(driver::ScenarioName name) {
    switch (name) {
    case driver::ScenarioName::AcIecBasic:
        return "AcIecBasic";
    case driver::ScenarioName::AcIecPauseResume:
        return "AcIecPauseResume";
    case driver::ScenarioName::AcIsoBasic:
        return "AcIsoBasic";
    case driver::ScenarioName::AcIsoD20Basic:
        return "AcIsoD20Basic";
    case driver::ScenarioName::DcIsoBasic:
        return "DcIsoBasic";
    case driver::ScenarioName::DcIsoD20Basic:
        return "DcIsoD20Basic";
    case driver::ScenarioName::DcIsoPauseResume:
        return "DcIsoPauseResume";
    case driver::ScenarioName::DcIsoBpt:
        return "DcIsoBpt";
    case driver::ScenarioName::DcIsoMcs:
        return "DcIsoMcs";
    case driver::ScenarioName::DiodeFailSmoke:
        return "DiodeFailSmoke";
    case driver::ScenarioName::AcIecRampUp:
        return "AcIecRampUp";
    case driver::ScenarioName::DcIsoTaper:
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

std::optional<int32_t> phase_value(const driver::ScenarioTimingOverrides& ov, Phase p) {
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
    const std::optional<driver::ScenarioTimingOverrides>& ov;
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

std::vector<ScenarioStep> build_scenario_steps(driver::ScenarioName name,
                                               const std::optional<driver::ScenarioTimingOverrides>& timing,
                                               std::string& out_reject_reason) {
    std::vector<ScenarioStep> steps;
    TimingResolver r{timing, {}, {}};
    switch (name) {
    case driver::ScenarioName::AcIecBasic: {
        api::AcIecSessionParams p;
        p.charging_current_a = 16.0f;
        p.three_phases = true;
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_command(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_command()});
        steps.push_back({r.at(Phase::StopAfter, 30000), make_stop_session_command()});
        break;
    }
    case driver::ScenarioName::AcIsoBasic: {
        api::AcIso2SessionParams p;
        p.charging_current_a = 16.0f;
        p.three_phases = true;
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_command(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_command()});
        steps.push_back({r.at(Phase::StopAfter, 60000), make_stop_session_command()});
        break;
    }
    case driver::ScenarioName::DcIsoBasic: {
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_command(api::DcIso2SessionParams{})});
        steps.push_back({std::chrono::milliseconds(0), make_plug_command()});
        break;
    }
    case driver::ScenarioName::AcIecPauseResume: {
        api::AcIecSessionParams p;
        p.charging_current_a = 16.0f;
        p.three_phases = true;
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_command(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_command()});
        steps.push_back({r.at(Phase::PauseAt, 30000), make_pause_session_command()});
        steps.push_back({r.at(Phase::ResumeAt, 60000), make_resume_session_command()});
        steps.push_back({r.at(Phase::StopAfter, 120000), make_stop_session_command()});
        steps.push_back({r.at(Phase::UnplugAfter, 125000), make_unplug_command()});
        break;
    }
    case driver::ScenarioName::DcIsoPauseResume: {
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_command(api::DcIso2SessionParams{})});
        steps.push_back({std::chrono::milliseconds(0), make_plug_command()});
        steps.push_back({r.at(Phase::PauseAt, 45000), make_pause_session_command()});
        steps.push_back({r.at(Phase::ResumeAt, 75000), make_resume_session_command()});
        steps.push_back({r.at(Phase::StopAfter, 180000), make_stop_session_command()});
        steps.push_back({r.at(Phase::UnplugAfter, 185000), make_unplug_command()});
        break;
    }
    case driver::ScenarioName::AcIsoD20Basic: {
        api::AcIsoD20SessionParams p;
        p.charging_current_a = 16.0f;
        p.three_phases = true;
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_command(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_command()});
        steps.push_back({r.at(Phase::StopAfter, 120000), make_stop_session_command()});
        steps.push_back({r.at(Phase::UnplugAfter, 125000), make_unplug_command()});
        break;
    }
    case driver::ScenarioName::DcIsoD20Basic: {
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_command(api::DcIsoD20SessionParams{})});
        steps.push_back({std::chrono::milliseconds(0), make_plug_command()});
        steps.push_back({r.at(Phase::StopAfter, 180000), make_stop_session_command()});
        steps.push_back({r.at(Phase::UnplugAfter, 185000), make_unplug_command()});
        break;
    }
    case driver::ScenarioName::DcIsoBpt: {
        api::DcIsoD20SessionParams p;
        api::BptParams bpt;
        bpt.discharge_max_current_limit = 50.0f;
        bpt.discharge_max_power_limit = 11000.0f;
        bpt.discharge_target_current = 30.0f;
        bpt.discharge_minimal_soc = 20.0f;
        p.bpt = bpt;
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_command(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_command()});
        steps.push_back({r.at(Phase::StopAfter, 180000), make_stop_session_command()});
        steps.push_back({r.at(Phase::UnplugAfter, 185000), make_unplug_command()});
        break;
    }
    case driver::ScenarioName::DcIsoMcs: {
        api::DcIsoD20SessionParams p;
        p.mcs_enabled = true;
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_command(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_command()});
        steps.push_back({r.at(Phase::StopAfter, 180000), make_stop_session_command()});
        steps.push_back({r.at(Phase::UnplugAfter, 185000), make_unplug_command()});
        break;
    }
    case driver::ScenarioName::AcIecRampUp: {
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
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_command(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_command()});
        steps.push_back({r.at(Phase::StopAfter, 60000), make_stop_session_command()});
        steps.push_back({r.at(Phase::UnplugAfter, 65000), make_unplug_command()});
        break;
    }
    case driver::ScenarioName::DcIsoTaper: {
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
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_command(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_command()});
        steps.push_back({r.at(Phase::StopAfter, 120000), make_stop_session_command()});
        steps.push_back({r.at(Phase::UnplugAfter, 125000), make_unplug_command()});
        break;
    }
    case driver::ScenarioName::DiodeFailSmoke: {
        api::AcIecSessionParams p;
        p.charging_current_a = 32.0f;
        p.three_phases = true;
        steps.push_back({std::chrono::milliseconds(0), make_configure_session_command(std::move(p))});
        steps.push_back({std::chrono::milliseconds(0), make_plug_command()});
        driver::InjectFaultParams fault;
        fault.type = driver::WorldFault::DiodeFail;
        steps.push_back({r.at(Phase::FaultAt, 15000), make_inject_fault_command(std::move(fault))});
        steps.push_back({r.at(Phase::ClearFaultAt, 20000), make_clear_fault_command()});
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

std::optional<std::string> ScenarioRunner::start(const API_scenario::RunScenarioParams& params, bool controller_seen,
                                                 bool pilot_passed, const Actions& actions, Clock::time_point now) {
    std::string reason;
    if (!controller_seen) {
        reason = "waiting for EvController heartbeat";
    } else if (active()) {
        reason = "scenario already running";
    } else if (pilot_passed) {
        reason = "already plugged";
    }
    auto steps = reason.empty() ? build_scenario_steps(params.name, params.timing, reason) : std::vector<Step>{};
    if (steps.empty()) {
        if (reason.empty()) {
            reason = "unknown scenario name";
        }
        actions.publish({params.name, API_scenario::ScenarioEventKind::Rejected, reason});
        return reason;
    }
    m_steps = std::move(steps);
    m_next = 1;
    m_name = params.name;
    m_pending_configuration = true;
    m_start = now;
    const auto& command = m_steps.front().command;
    if (const auto rejection = actions.execute(command.name, command.payload)) {
        reject(*rejection, actions);
        return rejection;
    }
    return std::nullopt;
}

void ScenarioRunner::configuration_ack(const std::optional<std::string>& rejection, const Actions& actions,
                                       Clock::time_point now) {
    if (!m_pending_configuration) {
        return;
    }
    if (now >= *next_deadline()) {
        on_timer(actions, now);
        return;
    }
    if (rejection) {
        reject(*rejection, actions);
        return;
    }
    m_pending_configuration = false;
    m_start = now;
    const auto& plug = m_steps[m_next].command;
    if (const auto rejection = actions.execute(plug.name, plug.payload)) {
        reject(*rejection, actions);
        return;
    }
    actions.publish({m_name, API_scenario::ScenarioEventKind::Started, std::nullopt});
    actions.publish({m_name, API_scenario::ScenarioEventKind::StepFired, m_steps.front().command.name});
    actions.publish({m_name, API_scenario::ScenarioEventKind::StepFired, plug.name});
    ++m_next;
    if (!active()) {
        actions.publish({m_name, API_scenario::ScenarioEventKind::Finished, std::nullopt});
    }
    on_timer(actions, now);
}

std::optional<ScenarioRunner::Clock::time_point> ScenarioRunner::next_deadline() const {
    if (!active()) {
        return std::nullopt;
    }
    return m_start + (m_pending_configuration ? std::chrono::milliseconds(10000) : m_steps[m_next].at);
}

std::string ScenarioRunner::current_step() const {
    if (m_steps.empty()) {
        return "unknown";
    }
    if (m_pending_configuration) {
        return m_steps.front().command.name;
    }
    return m_steps[m_next < m_steps.size() ? m_next : m_steps.size() - 1].command.name;
}

void ScenarioRunner::reject(const std::string& reason, const Actions& actions) {
    m_pending_configuration = false;
    m_steps.clear();
    m_next = 0;
    actions.publish({m_name, API_scenario::ScenarioEventKind::Rejected, reason});
}

void ScenarioRunner::on_timer(const Actions& actions, Clock::time_point now) {
    if (m_pending_configuration) {
        if (now >= *next_deadline()) {
            reject("timed out waiting for EvController configure_session acknowledgement", actions);
        }
        return;
    }
    while (active() && *next_deadline() <= now) {
        const auto& command = m_steps[m_next].command;
        if (const auto rejection = actions.execute(command.name, command.payload)) {
            reject(*rejection, actions);
            return;
        }
        actions.publish({m_name, API_scenario::ScenarioEventKind::StepFired, command.name});
        ++m_next;
        if (!active()) {
            actions.publish({m_name, API_scenario::ScenarioEventKind::Finished, std::nullopt});
        }
    }
}

} // namespace module
