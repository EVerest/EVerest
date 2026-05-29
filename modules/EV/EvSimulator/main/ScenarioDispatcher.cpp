// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "ScenarioDispatcher.hpp"

#include "Events.hpp"

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

void ac_iec_basic(FsmContext& ctx) {
    ctx.enqueue(make_plug_event());
    ctx.arm_scenario_timer(std::chrono::seconds(30));
    api::StartSessionParams p;
    p.mode = api::ChargeMode::AcIec;
    p.charging_current_a = 16.0f;
    p.three_phases = true;
    ctx.enqueue(make_start_session_event(std::move(p)));
}

void ac_iso_basic(FsmContext& ctx) {
    ctx.enqueue(make_plug_event());
    ctx.arm_scenario_timer(std::chrono::seconds(60));
    api::StartSessionParams p;
    p.mode = api::ChargeMode::AcIso2;
    p.charging_current_a = 16.0f;
    p.three_phases = true;
    ctx.enqueue(make_start_session_event(std::move(p)));
}

void dc_iso_basic(FsmContext& ctx) {
    ctx.enqueue(make_plug_event());
    api::StartSessionParams p;
    p.mode = api::ChargeMode::DcIso2;
    ctx.enqueue(make_start_session_event(std::move(p)));
}

} // namespace

void start_scenario(api::ScenarioName name, FsmContext& ctx) {
    switch (name) {
    case api::ScenarioName::AcIecBasic:
        ac_iec_basic(ctx);
        return;
    case api::ScenarioName::AcIsoBasic:
        ac_iso_basic(ctx);
        return;
    case api::ScenarioName::DcIsoBasic:
        dc_iso_basic(ctx);
        return;
    case api::ScenarioName::AcIecPauseResume:
    case api::ScenarioName::AcIsoD20Basic:
    case api::ScenarioName::DcIsoD20Basic:
    case api::ScenarioName::DcIsoPauseResume:
    case api::ScenarioName::DcIsoBpt:
    case api::ScenarioName::DcIsoMcs:
    case api::ScenarioName::DiodeFailSmoke:
        ctx.publish_e2m_command_ack("run_scenario", "scenario not implemented in v1");
        return;
    }
    ctx.publish_e2m_command_ack("run_scenario", "scenario not implemented in v1");
}

} // namespace module
