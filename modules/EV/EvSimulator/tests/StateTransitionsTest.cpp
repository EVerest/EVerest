// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/FsmContext.hpp"
#include "../main/states/BcbToggling.hpp"
#include "../main/states/Charging.hpp"
#include "../main/states/ChargingPwmPaused.hpp"
#include "../main/states/Disabled.hpp"
#include "../main/states/Faulted.hpp"
#include "../main/states/Paused.hpp"
#include "../main/states/Plugged.hpp"
#include "../main/states/SlacMatching.hpp"
#include "../main/states/Stopping.hpp"
#include "../main/states/Unplugged.hpp"
#include "../main/states/V2GNegotiating.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <everest_api_types/ev_simulator/codec.hpp>

#include <algorithm>
#include <string>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_simulator;

namespace {

bool contains_substr(const std::vector<std::string>& records, const std::string& needle) {
    return std::any_of(records.begin(), records.end(),
                       [&](const std::string& r) { return r.find(needle) != std::string::npos; });
}

bool topic_recorded(const PublisherSink& sink, const std::string& topic) {
    return std::any_of(sink.records.begin(), sink.records.end(), [&](const auto& kv) { return kv.first == topic; });
}

std::string payload_for(const PublisherSink& sink, const std::string& topic) {
    auto it = std::find_if(sink.records.begin(), sink.records.end(), [&](const auto& kv) { return kv.first == topic; });
    return it == sink.records.end() ? std::string{} : it->second;
}

} // namespace

TEST_CASE("FsmContext helpers and shortcuts", "[evsim][helpers]") {
    TestFixture fx;

    SECTION("transition_to_fault sets last_fault and returns Faulted") {
        auto ctx = fx.make_ctx();
        api::InjectFaultParams p{api::FaultType::RcdError, 30.0f};

        auto result = transition_to_fault(*ctx, p);

        REQUIRE(ctx->vars.last_fault.has_value());
        CHECK(ctx->vars.last_fault->type == api::FaultType::RcdError);
        CHECK(ctx->vars.last_fault->rcd_mA.has_value());
        CHECK(*ctx->vars.last_fault->rcd_mA == 30.0f);
        REQUIRE(result.new_state != nullptr);
        CHECK(result.new_state->get_id() == api::FsmState::Faulted);
        CHECK(result.unhandled == false);
    }

    SECTION("handle_query_state publishes state without transitioning") {
        auto ctx = fx.make_ctx();

        auto result = handle_query_state(*ctx, api::FsmState::Plugged);

        CHECK(result.new_state == nullptr);
        CHECK(result.unhandled == false);
        auto topic = fx.topics.everest_to_extern("state");
        REQUIRE(topic_recorded(fx.sink, topic));
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, topic));
        CHECK(decoded == api::FsmState::Plugged);
    }

    SECTION("transition_to_disabled clears BSP, ISO, persistence and saves KVS") {
        fx.cfg.keep_cross_boot_plugin_state = true;
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIso2;
        ctx->vars.last_fault = api::FaultReport{api::FaultType::DiodeFail, std::nullopt, std::nullopt};
        ctx->persisted.plugged_in = true;

        auto result = transition_to_disabled(*ctx);

        // BSP fault state cleared.
        CHECK(contains_substr(fx.mocks.bsp.records, "diode_fail(value=false)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "set_rcd_error(rcd_current_mA=0)"));
        // CP=A and power off.
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=A)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=false)"));
        // ISO stop invoked.
        CHECK(contains_substr(fx.mocks.iso.records, "stop_charging()"));
        // SimVars resets.
        CHECK_FALSE(ctx->vars.charge_mode.has_value());
        CHECK_FALSE(ctx->vars.last_fault.has_value());
        // Persisted.plugged_in cleared and KVS save invoked.
        CHECK(ctx->persisted.plugged_in == false);
        CHECK(contains_substr(fx.mocks.kvs.records, "store(key=evsim_1_state"));
        // New state is Disabled.
        REQUIRE(result.new_state != nullptr);
        CHECK(result.new_state->get_id() == api::FsmState::Disabled);
    }

    SECTION("set_cp routes through peer_actions") {
        auto ctx = fx.make_ctx();
        ctx->set_cp(::types::ev_board_support::EvCpState::B);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));
    }

    SECTION("allow_power_on routes through peer_actions") {
        auto ctx = fx.make_ctx();
        ctx->allow_power_on(true);
        CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=true)"));
    }

    SECTION("bsp_apply_ac_params records calls and updates SimVars") {
        auto ctx = fx.make_ctx();
        ctx->bsp_apply_ac_params(16.0f, true);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=16)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "set_three_phases(three_phases=true)"));
        CHECK(ctx->vars.charging_current_a == 16.0f);
        CHECK(ctx->vars.three_phases == true);
    }

    SECTION("iso_start_charging(AcIec) returns false without action call") {
        auto ctx = fx.make_ctx();
        auto ok = ctx->iso_start_charging(api::ChargeMode::AcIec, std::nullopt, 0, 0);
        CHECK(ok == false);
        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "start_charging"));
    }

    SECTION("iso_start_charging(AcIso2, three_phases=true) maps to AC_three_phase_core") {
        auto ctx = fx.make_ctx();
        ctx->vars.three_phases = true;
        auto ok = ctx->iso_start_charging(api::ChargeMode::AcIso2, api::PaymentOption::ExternalPayment, 100, 5000);
        CHECK(ok == true); // MockIso15118Ev defaults next_start_charging_result=true
        CHECK(contains_substr(fx.mocks.iso.records, "mode=AC_three_phase_core"));
        CHECK(contains_substr(fx.mocks.iso.records, "payment=ExternalPayment"));
    }

    SECTION("iso_start_charging(AcIso2, three_phases=false) maps to AC_single_phase_core") {
        auto ctx = fx.make_ctx();
        ctx->vars.three_phases = false;
        ctx->iso_start_charging(api::ChargeMode::AcIso2, std::nullopt, 0, 0);
        CHECK(contains_substr(fx.mocks.iso.records, "mode=AC_single_phase_core"));
    }

    SECTION("iso_start_charging(DcIso2) maps to DC_extended") {
        auto ctx = fx.make_ctx();
        ctx->iso_start_charging(api::ChargeMode::DcIso2, std::nullopt, 0, 0);
        CHECK(contains_substr(fx.mocks.iso.records, "mode=DC_extended"));
    }

    SECTION("iso_start_charging(AcIsoD20) returns false without action call") {
        auto ctx = fx.make_ctx();
        auto ok = ctx->iso_start_charging(api::ChargeMode::AcIsoD20, std::nullopt, 0, 0);
        CHECK(ok == false);
        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "start_charging"));
    }

    SECTION("iso_start_charging(DcIsoD20) returns false without action call") {
        auto ctx = fx.make_ctx();
        auto ok = ctx->iso_start_charging(api::ChargeMode::DcIsoD20, std::nullopt, 0, 0);
        CHECK(ok == false);
        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "start_charging"));
    }

    SECTION("iso_start_charging passes Contract through") {
        auto ctx = fx.make_ctx();
        ctx->iso_start_charging(api::ChargeMode::AcIso2, api::PaymentOption::Contract, 0, 0);
        CHECK(contains_substr(fx.mocks.iso.records, "payment=Contract"));
    }

    SECTION("iso_update_soc records call") {
        auto ctx = fx.make_ctx();
        ctx->iso_update_soc(45.5f);
        CHECK(contains_substr(fx.mocks.iso.records, "update_soc(SoC=45.5)"));
    }

    SECTION("slac_trigger_matching records call and returns mock result") {
        auto ctx = fx.make_ctx();
        fx.mocks.slac.next_trigger_matching_result = true;
        CHECK(ctx->slac_trigger_matching() == true);
        CHECK(contains_substr(fx.mocks.slac.records, "trigger_matching()"));
    }

    SECTION("kvs_save records key+json when keep_cross_boot_plugin_state=true") {
        fx.cfg.keep_cross_boot_plugin_state = true;
        auto ctx = fx.make_ctx();
        ctx->persisted.plugged_in = true;
        ctx->persisted.last_mode = api::ChargeMode::AcIec;

        ctx->kvs_save();

        REQUIRE_FALSE(fx.mocks.kvs.records.empty());
        const auto& rec = fx.mocks.kvs.records.back();
        CHECK(rec.find("store(key=evsim_1_state") != std::string::npos);
        // JSON round-trip via nlohmann should restore the same persisted state.
        auto json_start = rec.find("json=");
        REQUIRE(json_start != std::string::npos);
        auto json_str = rec.substr(json_start + 5);
        // strip trailing ')'
        REQUIRE(!json_str.empty());
        if (json_str.back() == ')') {
            json_str.pop_back();
        }
        auto roundtrip = nlohmann::json::parse(json_str).get<PersistedState>();
        CHECK(roundtrip.plugged_in == true);
        REQUIRE(roundtrip.last_mode.has_value());
        CHECK(*roundtrip.last_mode == api::ChargeMode::AcIec);
    }

    SECTION("kvs_save records NOTHING when keep_cross_boot_plugin_state=false") {
        fx.cfg.keep_cross_boot_plugin_state = false;
        auto ctx = fx.make_ctx();
        ctx->persisted.plugged_in = true;

        ctx->kvs_save();

        CHECK(fx.mocks.kvs.records.empty());
    }

    SECTION("kvs_load from empty string leaves persisted at defaults") {
        fx.cfg.keep_cross_boot_plugin_state = true;
        fx.mocks.next_kvs_load_value = ""; // simulates missing key
        auto ctx = fx.make_ctx();

        ctx->kvs_load();

        // defaults: plugged_in=false, last_mode=nullopt, last_scenario=nullopt
        CHECK(ctx->persisted.plugged_in == false);
        CHECK_FALSE(ctx->persisted.last_mode.has_value());
        CHECK_FALSE(ctx->persisted.last_scenario.has_value());
    }

    SECTION("kvs_load from valid JSON populates persisted") {
        fx.cfg.keep_cross_boot_plugin_state = true;
        PersistedState seed{};
        seed.plugged_in = true;
        seed.last_mode = api::ChargeMode::DcIso2;
        fx.mocks.next_kvs_load_value = nlohmann::json(seed).dump();
        auto ctx = fx.make_ctx();

        ctx->kvs_load();

        CHECK(ctx->persisted.plugged_in == true);
        REQUIRE(ctx->persisted.last_mode.has_value());
        CHECK(*ctx->persisted.last_mode == api::ChargeMode::DcIso2);
    }

    SECTION("publish_e2m_state records topic+payload and updates snapshot") {
        auto ctx = fx.make_ctx();
        ctx->publish_e2m_state(api::FsmState::Plugged);
        auto topic = fx.topics.everest_to_extern("state");
        REQUIRE(topic_recorded(fx.sink, topic));
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, topic));
        CHECK(decoded == api::FsmState::Plugged);
        CHECK(ctx->snapshot.handle()->current_state == api::FsmState::Plugged);
    }

    SECTION("publish_e2m_command_ack records Rejected ack on command_ack topic") {
        auto ctx = fx.make_ctx();
        ctx->publish_e2m_command_ack("start_session", "no_session_active");
        auto topic = fx.topics.everest_to_extern("command_ack");
        REQUIRE(topic_recorded(fx.sink, topic));
        auto decoded = api::deserialize<api::CommandAck>(payload_for(fx.sink, topic));
        CHECK(decoded.command == "start_session");
        CHECK(decoded.status == api::CommandAckStatus::Rejected);
        REQUIRE(decoded.reason.has_value());
        CHECK(*decoded.reason == "no_session_active");
    }

    SECTION("publish_e2m_fault updates snapshot.last_fault") {
        auto ctx = fx.make_ctx();
        api::FaultReport f{api::FaultType::SlacTimeout, std::nullopt, std::nullopt};
        ctx->publish_e2m_fault(f);
        CHECK(ctx->snapshot.handle()->last_fault.has_value());
        CHECK(ctx->snapshot.handle()->last_fault->type == api::FaultType::SlacTimeout);
    }

    SECTION("publish_e2m_ev_info updates snapshot.soc_pct") {
        auto ctx = fx.make_ctx();
        ctx->vars.soc_pct = 75.0f;
        ctx->publish_e2m_ev_info();
        CHECK(ctx->snapshot.handle()->soc_pct == 75.0f);
    }
}

TEST_CASE("EvSimulator group1 transitions", "[evsim][group1]") {
    TestFixture fx;
    const auto state_topic = fx.topics.everest_to_extern("state");
    const auto ack_topic = fx.topics.everest_to_extern("command_ack");

    // ---- Disabled ------------------------------------------------------

    SECTION("Disabled.enter publishes state=Disabled") {
        auto ctx = fx.make_ctx();
        Disabled s{*ctx};
        s.enter();
        REQUIRE(topic_recorded(fx.sink, state_topic));
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Disabled);
    }

    SECTION("Disabled: Enable -> Unplugged") {
        auto ctx = fx.make_ctx();
        Disabled s{*ctx};
        auto result = s.feed(Event{EventKind::Enable});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
        CHECK(result.unhandled == false);
    }

    SECTION("Disabled: QueryState publishes state, no transition") {
        auto ctx = fx.make_ctx();
        Disabled s{*ctx};
        auto result = s.feed(Event{EventKind::QueryState});
        CHECK(result.new_state == nullptr);
        REQUIRE(topic_recorded(fx.sink, state_topic));
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Disabled);
    }

    SECTION("Disabled: Disable is unhandled no-op") {
        auto ctx = fx.make_ctx();
        Disabled s{*ctx};
        auto result = s.feed(Event{EventKind::Disable});
        CHECK(result.new_state == nullptr);
        CHECK(result.unhandled == true);
    }

    SECTION("Disabled: InjectFault rejected with explanatory ack") {
        auto ctx = fx.make_ctx();
        Disabled s{*ctx};
        Event ev{EventKind::InjectFault};
        ev.payload = api::InjectFaultParams{api::FaultType::DiodeFail, std::nullopt};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        REQUIRE(topic_recorded(fx.sink, ack_topic));
        auto ack = api::deserialize<api::CommandAck>(payload_for(fx.sink, ack_topic));
        CHECK(ack.command == "inject_fault");
        CHECK(ack.status == api::CommandAckStatus::Rejected);
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "InjectFault requires session");
    }

    SECTION("Disabled: StartSession rejected with 'module disabled'") {
        auto ctx = fx.make_ctx();
        Disabled s{*ctx};
        Event ev{EventKind::StartSession};
        ev.payload = api::StartSessionParams{api::ChargeMode::AcIec, std::nullopt, std::nullopt,
                                             std::nullopt,           std::nullopt, std::nullopt};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        auto ack = api::deserialize<api::CommandAck>(payload_for(fx.sink, ack_topic));
        CHECK(ack.command == "start_session");
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "module disabled");
    }

    SECTION("Disabled: peer events are unhandled silently") {
        auto ctx = fx.make_ctx();
        Disabled s{*ctx};
        auto result = s.feed(Event{EventKind::BspEvent});
        CHECK(result.new_state == nullptr);
        CHECK(result.unhandled == true);
    }

    // ---- Unplugged -----------------------------------------------------

    SECTION("Unplugged.enter clears BSP, ISO, persistence and publishes Unplugged") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIso2;
        ctx->vars.last_fault = api::FaultReport{api::FaultType::RcdError, std::nullopt, std::nullopt};
        ctx->persisted.plugged_in = true;
        Unplugged s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=A)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=false)"));
        CHECK(contains_substr(fx.mocks.iso.records, "stop_charging()"));
        CHECK_FALSE(ctx->vars.charge_mode.has_value());
        CHECK_FALSE(ctx->vars.last_fault.has_value());
        CHECK(ctx->persisted.plugged_in == false);
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Unplugged);
    }

    SECTION("Unplugged: Plug -> Plugged") {
        auto ctx = fx.make_ctx();
        Unplugged s{*ctx};
        auto result = s.feed(Event{EventKind::Plug});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Plugged);
    }

    SECTION("Unplugged: Disable -> Disabled via transition_to_disabled") {
        auto ctx = fx.make_ctx();
        Unplugged s{*ctx};
        auto result = s.feed(Event{EventKind::Disable});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Disabled);
    }

    SECTION("Unplugged: SetSoc updates SoC and battery charge, no transition") {
        auto ctx = fx.make_ctx();
        Unplugged s{*ctx};
        Event ev{EventKind::SetSoc};
        ev.payload = api::SetSocParams{50.0f};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(ctx->vars.soc_pct == 50.0f);
        CHECK(ctx->vars.battery_charge_wh == 0.5f * ctx->vars.battery_capacity_wh);
    }

    SECTION("Unplugged: RunScenario AcIecBasic dispatches without ack") {
        auto ctx = fx.make_ctx();
        Unplugged s{*ctx};
        Event ev{EventKind::RunScenario};
        ev.payload = api::RunScenarioParams{api::ScenarioName::AcIecBasic};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        // Implemented presets enqueue events through the runtime seam and do
        // not publish a command_ack on the e2m channel.
        CHECK(payload_for(fx.sink, ack_topic).empty());
    }

    SECTION("Unplugged: RunScenario unimplemented preset rejects via ack") {
        auto ctx = fx.make_ctx();
        Unplugged s{*ctx};
        Event ev{EventKind::RunScenario};
        ev.payload = api::RunScenarioParams{api::ScenarioName::DcIsoBpt};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        auto ack = api::deserialize<api::CommandAck>(payload_for(fx.sink, ack_topic));
        CHECK(ack.command == "run_scenario");
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "scenario not implemented in v1");
    }

    SECTION("Unplugged: QueryState publishes Unplugged, no transition") {
        auto ctx = fx.make_ctx();
        Unplugged s{*ctx};
        auto result = s.feed(Event{EventKind::QueryState});
        CHECK(result.new_state == nullptr);
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Unplugged);
    }

    SECTION("Unplugged: StartSession rejected (no session active)") {
        auto ctx = fx.make_ctx();
        Unplugged s{*ctx};
        Event ev{EventKind::StartSession};
        ev.payload = api::StartSessionParams{api::ChargeMode::AcIec, std::nullopt, std::nullopt,
                                             std::nullopt,           std::nullopt, std::nullopt};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        auto ack = api::deserialize<api::CommandAck>(payload_for(fx.sink, ack_topic));
        CHECK(ack.command == "start_session");
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "no session active");
    }

    SECTION("Unplugged: Unplug is no-op") {
        auto ctx = fx.make_ctx();
        Unplugged s{*ctx};
        auto result = s.feed(Event{EventKind::Unplug});
        CHECK(result.new_state == nullptr);
        CHECK(result.unhandled == true);
    }

    // ---- Plugged -------------------------------------------------------

    SECTION("Plugged.enter sets CP=B, power off, persists and publishes Plugged") {
        fx.cfg.keep_cross_boot_plugin_state = true;
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=false)"));
        CHECK(ctx->persisted.plugged_in == true);
        CHECK(contains_substr(fx.mocks.kvs.records, "store(key=evsim_1_state"));
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Plugged);
    }

    SECTION("Plugged: StartSession(AcIec) applies AC params, stays in Plugged") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};
        Event ev{EventKind::StartSession};
        ev.payload =
            api::StartSessionParams{api::ChargeMode::AcIec, std::nullopt, std::nullopt, std::nullopt, 20.0f, false};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(ctx->vars.charge_mode == api::ChargeMode::AcIec);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=20)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "set_three_phases(three_phases=false)"));
    }

    SECTION("Plugged: StartSession(AcIso2) -> SlacMatching") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};
        Event ev{EventKind::StartSession};
        ev.payload = api::StartSessionParams{
            api::ChargeMode::AcIso2, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::SlacMatching);
        CHECK(ctx->vars.charge_mode == api::ChargeMode::AcIso2);
        // AC params applied for AcIso2 path
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current"));
    }

    SECTION("Plugged: StartSession(DcIso2) -> SlacMatching, no AC params") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};
        Event ev{EventKind::StartSession};
        ev.payload = api::StartSessionParams{
            api::ChargeMode::DcIso2, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::SlacMatching);
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_ac_max_current"));
    }

    SECTION("Plugged: StartSession(AcIsoD20) rejected with iso15118-20 reason") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};
        Event ev{EventKind::StartSession};
        ev.payload = api::StartSessionParams{
            api::ChargeMode::AcIsoD20, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK_FALSE(ctx->vars.charge_mode.has_value());
        auto ack = api::deserialize<api::CommandAck>(payload_for(fx.sink, ack_topic));
        CHECK(ack.command == "start_session");
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "iso15118-20 not supported in v1");
    }

    SECTION("Plugged: StartSession(AcIso2) without slac peer rejected") {
        auto ctx = fx.make_ctx();
        // Clear iso + slac action wiring to simulate missing peers.
        ctx->peer_actions.iso_start_charging = nullptr;
        ctx->peer_actions.slac_trigger_matching = nullptr;
        Plugged s{*ctx};
        Event ev{EventKind::StartSession};
        ev.payload = api::StartSessionParams{
            api::ChargeMode::AcIso2, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK_FALSE(ctx->vars.charge_mode.has_value());
        auto ack = api::deserialize<api::CommandAck>(payload_for(fx.sink, ack_topic));
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "no ev_slac peer");
    }

    SECTION("Plugged: BspMeasurement triggers Charging when AcIec PWM in range") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIec;
        Plugged s{*ctx};
        Event ev{EventKind::BspMeasurement};
        BspMeasurementPayload m;
        m.cp_pwm_duty_cycle = 50;
        m.rcd_current_mA = std::nullopt;
        ev.payload = m;
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Charging);
        CHECK(ctx->vars.pwm_duty_cycle == 50);
    }

    SECTION("Plugged: BspMeasurement out-of-range PWM stays in Plugged") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIec;
        Plugged s{*ctx};
        Event ev{EventKind::BspMeasurement};
        BspMeasurementPayload m;
        m.cp_pwm_duty_cycle = 5; // below threshold (7)
        m.rcd_current_mA = std::nullopt;
        ev.payload = m;
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(ctx->vars.pwm_duty_cycle == 5);
    }

    SECTION("Plugged: Unplug -> Unplugged") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};
        auto result = s.feed(Event{EventKind::Unplug});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("Plugged: BspEvent Disconnected -> Unplugged") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};
        Event ev{EventKind::BspEvent};
        BspEventPayload p;
        p.bsp_event.event = ::types::board_support_common::Event::Disconnected;
        ev.payload = p;
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("Plugged: BspEvent non-Disconnected stays") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};
        Event ev{EventKind::BspEvent};
        BspEventPayload p;
        p.bsp_event.event = ::types::board_support_common::Event::B;
        ev.payload = p;
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(result.unhandled == true);
    }

    SECTION("Plugged: InjectFault -> Faulted") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};
        Event ev{EventKind::InjectFault};
        ev.payload = api::InjectFaultParams{api::FaultType::DiodeFail, std::nullopt};
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Faulted);
        REQUIRE(ctx->vars.last_fault.has_value());
        CHECK(ctx->vars.last_fault->type == api::FaultType::DiodeFail);
    }

    SECTION("Plugged: Disable -> Disabled") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};
        auto result = s.feed(Event{EventKind::Disable});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Disabled);
    }

    SECTION("Plugged: StopSession rejected (no session active)") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};
        auto result = s.feed(Event{EventKind::StopSession});
        CHECK(result.new_state == nullptr);
        auto ack = api::deserialize<api::CommandAck>(payload_for(fx.sink, ack_topic));
        CHECK(ack.command == "stop_session");
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "no session active");
    }

    // ---- SlacMatching --------------------------------------------------

    SECTION("SlacMatching.enter triggers SLAC, arms timer, publishes state") {
        auto ctx = fx.make_ctx();
        SlacMatching s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.slac.records, "trigger_matching()"));
        CHECK(ctx->vars.slac_state == "MATCHING");
        REQUIRE(fx.timer.state_timer_arms.size() == 1);
        CHECK(fx.timer.state_timer_arms[0] == std::chrono::seconds(30));
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::SlacMatching);
    }

    SECTION("SlacMatching: SlacState=MATCHED -> V2GNegotiating") {
        auto ctx = fx.make_ctx();
        SlacMatching s{*ctx};
        Event ev{EventKind::SlacState};
        ev.payload = SlacStatePayload{"MATCHED"};
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::V2GNegotiating);
    }

    SECTION("SlacMatching: SlacState != MATCHED stays") {
        auto ctx = fx.make_ctx();
        SlacMatching s{*ctx};
        Event ev{EventKind::SlacState};
        ev.payload = SlacStatePayload{"MATCHING"};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(result.unhandled == true);
    }

    SECTION("SlacMatching: StateDeadline -> Faulted (SlacTimeout)") {
        auto ctx = fx.make_ctx();
        SlacMatching s{*ctx};
        auto result = s.feed(Event{EventKind::StateDeadline});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Faulted);
        REQUIRE(ctx->vars.last_fault.has_value());
        CHECK(ctx->vars.last_fault->type == api::FaultType::SlacTimeout);
        REQUIRE(ctx->vars.last_fault->message.has_value());
        CHECK(*ctx->vars.last_fault->message == "SLAC match deadline exceeded");
    }

    SECTION("SlacMatching: Unplug -> Unplugged") {
        auto ctx = fx.make_ctx();
        SlacMatching s{*ctx};
        auto result = s.feed(Event{EventKind::Unplug});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("SlacMatching: BspEvent Disconnected -> Unplugged") {
        auto ctx = fx.make_ctx();
        SlacMatching s{*ctx};
        Event ev{EventKind::BspEvent};
        BspEventPayload p;
        p.bsp_event.event = ::types::board_support_common::Event::Disconnected;
        ev.payload = p;
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("SlacMatching: InjectFault -> Faulted") {
        auto ctx = fx.make_ctx();
        SlacMatching s{*ctx};
        Event ev{EventKind::InjectFault};
        ev.payload = api::InjectFaultParams{api::FaultType::RcdError, 30.0f};
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Faulted);
    }

    SECTION("SlacMatching: Disable -> Disabled") {
        auto ctx = fx.make_ctx();
        SlacMatching s{*ctx};
        auto result = s.feed(Event{EventKind::Disable});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Disabled);
    }

    SECTION("SlacMatching: StartSession rejected (slac matching in progress)") {
        auto ctx = fx.make_ctx();
        SlacMatching s{*ctx};
        Event ev{EventKind::StartSession};
        ev.payload = api::StartSessionParams{api::ChargeMode::AcIec, std::nullopt, std::nullopt,
                                             std::nullopt,           std::nullopt, std::nullopt};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        auto ack = api::deserialize<api::CommandAck>(payload_for(fx.sink, ack_topic));
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "slac matching in progress");
    }

    SECTION("SlacMatching: QueryState publishes SlacMatching") {
        auto ctx = fx.make_ctx();
        SlacMatching s{*ctx};
        // clear sink so we don't pick up the enter() publication
        fx.sink.clear();
        auto result = s.feed(Event{EventKind::QueryState});
        CHECK(result.new_state == nullptr);
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::SlacMatching);
    }

    // ---- V2GNegotiating -------------------------------------------------

    SECTION("V2GNegotiating.enter starts ISO charging when charge_mode set, arms 60s") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIso2;
        ctx->vars.three_phases = true;
        V2GNegotiating s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.iso.records, "start_charging"));
        REQUIRE(fx.timer.state_timer_arms.size() == 1);
        CHECK(fx.timer.state_timer_arms[0] == std::chrono::seconds(60));
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::V2GNegotiating);
    }

    SECTION("V2GNegotiating.enter skips iso_start when charge_mode missing") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode.reset();
        V2GNegotiating s{*ctx};
        s.enter();
        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "start_charging"));
        REQUIRE(fx.timer.state_timer_arms.size() == 1);
        CHECK(fx.timer.state_timer_arms[0] == std::chrono::seconds(60));
    }

    SECTION("V2GNegotiating: IsoPowerReady -> Charging") {
        auto ctx = fx.make_ctx();
        V2GNegotiating s{*ctx};
        auto result = s.feed(Event{EventKind::IsoPowerReady});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Charging);
    }

    SECTION("V2GNegotiating: IsoStopFromCharger -> Stopping") {
        auto ctx = fx.make_ctx();
        V2GNegotiating s{*ctx};
        auto result = s.feed(Event{EventKind::IsoStopFromCharger});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Stopping);
    }

    SECTION("V2GNegotiating: Unplug -> Stopping") {
        auto ctx = fx.make_ctx();
        V2GNegotiating s{*ctx};
        auto result = s.feed(Event{EventKind::Unplug});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Stopping);
    }

    SECTION("V2GNegotiating: StateDeadline -> Faulted (V2GTimeout)") {
        auto ctx = fx.make_ctx();
        V2GNegotiating s{*ctx};
        auto result = s.feed(Event{EventKind::StateDeadline});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Faulted);
        REQUIRE(ctx->vars.last_fault.has_value());
        CHECK(ctx->vars.last_fault->type == api::FaultType::V2GTimeout);
        REQUIRE(ctx->vars.last_fault->message.has_value());
        CHECK(*ctx->vars.last_fault->message == "V2G negotiation deadline exceeded");
    }

    SECTION("V2GNegotiating: BspEvent Disconnected -> Unplugged") {
        auto ctx = fx.make_ctx();
        V2GNegotiating s{*ctx};
        Event ev{EventKind::BspEvent};
        BspEventPayload p;
        p.bsp_event.event = ::types::board_support_common::Event::Disconnected;
        ev.payload = p;
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("V2GNegotiating: InjectFault -> Faulted") {
        auto ctx = fx.make_ctx();
        V2GNegotiating s{*ctx};
        Event ev{EventKind::InjectFault};
        ev.payload = api::InjectFaultParams{api::FaultType::Internal, std::nullopt};
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Faulted);
    }

    SECTION("V2GNegotiating: Disable -> Disabled") {
        auto ctx = fx.make_ctx();
        V2GNegotiating s{*ctx};
        auto result = s.feed(Event{EventKind::Disable});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Disabled);
    }

    SECTION("V2GNegotiating: StopSession rejected (negotiation in progress)") {
        auto ctx = fx.make_ctx();
        V2GNegotiating s{*ctx};
        auto result = s.feed(Event{EventKind::StopSession});
        CHECK(result.new_state == nullptr);
        auto ack = api::deserialize<api::CommandAck>(payload_for(fx.sink, ack_topic));
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "negotiation in progress");
    }
}

TEST_CASE("EvSimulator group2 transitions", "[evsim][group2]") {
    TestFixture fx;
    const auto state_topic = fx.topics.everest_to_extern("state");
    const auto ack_topic = fx.topics.everest_to_extern("command_ack");
    const auto fault_topic = fx.topics.everest_to_extern("fault");

    // ---- Charging ------------------------------------------------------

    SECTION("Charging.enter sets CP=C, power on, arms tick, publishes Charging") {
        auto ctx = fx.make_ctx();
        Charging s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=C)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=true)"));
        REQUIRE_FALSE(fx.timer.tick_arms.empty());
        CHECK(fx.timer.tick_arms.back() == ctx->cfg.tick_interval_ms);
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Charging);
    }

    SECTION("Charging.leave disarms tick and cancels state timer") {
        auto ctx = fx.make_ctx();
        Charging s{*ctx};
        s.enter();
        fx.timer.clear();
        s.leave();
        CHECK(fx.timer.tick_disarms == 1);
        CHECK(fx.timer.state_timer_cancels == 1);
    }

    SECTION("Charging: StopSession in AcIec -> Unplugged") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIec;
        Charging s{*ctx};
        auto result = s.feed(Event{EventKind::StopSession});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("Charging: StopSession in AcIso2 -> Stopping") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIso2;
        Charging s{*ctx};
        auto result = s.feed(Event{EventKind::StopSession});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Stopping);
    }

    SECTION("Charging: StopSession in DcIso2 -> Stopping") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::DcIso2;
        Charging s{*ctx};
        auto result = s.feed(Event{EventKind::StopSession});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Stopping);
    }

    SECTION("Charging: PauseSession -> Paused") {
        auto ctx = fx.make_ctx();
        Charging s{*ctx};
        auto result = s.feed(Event{EventKind::PauseSession});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Paused);
    }

    SECTION("Charging: SetChargingCurrent in AcIec applies AC params") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIec;
        Charging s{*ctx};
        Event ev{EventKind::SetChargingCurrent};
        ev.payload = api::SetChargingCurrentParams{12.5f, false};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=12.5)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "set_three_phases(three_phases=false)"));
    }

    SECTION("Charging: SetChargingCurrent in DcIso2 rejected with DC reason") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::DcIso2;
        Charging s{*ctx};
        Event ev{EventKind::SetChargingCurrent};
        ev.payload = api::SetChargingCurrentParams{20.0f, true};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        auto ack = api::deserialize<api::CommandAck>(payload_for(fx.sink, ack_topic));
        CHECK(ack.command == "set_charging_current");
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "set_charging_current not supported in DC/ISO-20 mode");
    }

    SECTION("Charging: IsoStopFromCharger -> Stopping") {
        auto ctx = fx.make_ctx();
        Charging s{*ctx};
        auto result = s.feed(Event{EventKind::IsoStopFromCharger});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Stopping);
    }

    SECTION("Charging: IsoPauseFromCharger -> Paused") {
        auto ctx = fx.make_ctx();
        Charging s{*ctx};
        auto result = s.feed(Event{EventKind::IsoPauseFromCharger});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Paused);
    }

    SECTION("Charging: BspMeasurement out-of-window (PWM<=7) in AcIec -> ChargingPwmPaused") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIec;
        Charging s{*ctx};
        Event ev{EventKind::BspMeasurement};
        BspMeasurementPayload m;
        m.cp_pwm_duty_cycle = 5;
        m.rcd_current_mA = std::nullopt;
        ev.payload = m;
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::ChargingPwmPaused);
        CHECK(ctx->vars.pwm_duty_cycle == 5);
    }

    SECTION("Charging: BspMeasurement out-of-window (PWM>=97) in AcIec -> ChargingPwmPaused") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIec;
        Charging s{*ctx};
        Event ev{EventKind::BspMeasurement};
        BspMeasurementPayload m;
        m.cp_pwm_duty_cycle = 98;
        m.rcd_current_mA = std::nullopt;
        ev.payload = m;
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::ChargingPwmPaused);
    }

    SECTION("Charging: BspMeasurement in-window stays in Charging") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIec;
        Charging s{*ctx};
        Event ev{EventKind::BspMeasurement};
        BspMeasurementPayload m;
        m.cp_pwm_duty_cycle = 50;
        m.rcd_current_mA = std::nullopt;
        ev.payload = m;
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(ctx->vars.pwm_duty_cycle == 50);
    }

    SECTION("Charging: BspMeasurement out-of-window in DcIso2 stays in Charging") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::DcIso2;
        Charging s{*ctx};
        Event ev{EventKind::BspMeasurement};
        BspMeasurementPayload m;
        m.cp_pwm_duty_cycle = 5;
        m.rcd_current_mA = std::nullopt;
        ev.payload = m;
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
    }

    SECTION("Charging: Unplug in AcIec -> Unplugged") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIec;
        Charging s{*ctx};
        auto result = s.feed(Event{EventKind::Unplug});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("Charging: Unplug in DcIso2 -> Stopping") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::DcIso2;
        Charging s{*ctx};
        auto result = s.feed(Event{EventKind::Unplug});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Stopping);
    }

    SECTION("Charging: BspEvent Disconnected -> Unplugged") {
        auto ctx = fx.make_ctx();
        Charging s{*ctx};
        Event ev{EventKind::BspEvent};
        BspEventPayload p;
        p.bsp_event.event = ::types::board_support_common::Event::Disconnected;
        ev.payload = p;
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("Charging: InjectFault -> Faulted") {
        auto ctx = fx.make_ctx();
        Charging s{*ctx};
        Event ev{EventKind::InjectFault};
        ev.payload = api::InjectFaultParams{api::FaultType::DiodeFail, std::nullopt};
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Faulted);
    }

    SECTION("Charging: Disable -> Disabled") {
        auto ctx = fx.make_ctx();
        Charging s{*ctx};
        auto result = s.feed(Event{EventKind::Disable});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Disabled);
    }

    SECTION("Charging: QueryState publishes Charging without transition") {
        auto ctx = fx.make_ctx();
        Charging s{*ctx};
        fx.sink.clear();
        auto result = s.feed(Event{EventKind::QueryState});
        CHECK(result.new_state == nullptr);
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Charging);
    }

    // ---- ChargingPwmPaused ---------------------------------------------

    SECTION("ChargingPwmPaused.enter sets CP=B, power off, publishes state") {
        auto ctx = fx.make_ctx();
        ChargingPwmPaused s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=false)"));
        // No state_timer armed.
        CHECK(fx.timer.state_timer_arms.empty());
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::ChargingPwmPaused);
    }

    SECTION("ChargingPwmPaused: BspMeasurement in-window -> Charging") {
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIec;
        ChargingPwmPaused s{*ctx};
        Event ev{EventKind::BspMeasurement};
        BspMeasurementPayload m;
        m.cp_pwm_duty_cycle = 50;
        m.rcd_current_mA = std::nullopt;
        ev.payload = m;
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Charging);
        CHECK(ctx->vars.pwm_duty_cycle == 50);
    }

    SECTION("ChargingPwmPaused: BspMeasurement still out-of-window stays") {
        auto ctx = fx.make_ctx();
        ChargingPwmPaused s{*ctx};
        Event ev{EventKind::BspMeasurement};
        BspMeasurementPayload m;
        m.cp_pwm_duty_cycle = 5;
        m.rcd_current_mA = std::nullopt;
        ev.payload = m;
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
    }

    SECTION("ChargingPwmPaused: Unplug -> Unplugged") {
        auto ctx = fx.make_ctx();
        ChargingPwmPaused s{*ctx};
        auto result = s.feed(Event{EventKind::Unplug});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("ChargingPwmPaused: BspEvent Disconnected -> Unplugged") {
        auto ctx = fx.make_ctx();
        ChargingPwmPaused s{*ctx};
        Event ev{EventKind::BspEvent};
        BspEventPayload p;
        p.bsp_event.event = ::types::board_support_common::Event::Disconnected;
        ev.payload = p;
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("ChargingPwmPaused: StopSession -> Unplugged (AC IEC)") {
        auto ctx = fx.make_ctx();
        ChargingPwmPaused s{*ctx};
        auto result = s.feed(Event{EventKind::StopSession});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("ChargingPwmPaused: PauseSession rejected with ISO-not-applicable reason") {
        auto ctx = fx.make_ctx();
        ChargingPwmPaused s{*ctx};
        auto result = s.feed(Event{EventKind::PauseSession});
        CHECK(result.new_state == nullptr);
        auto ack = api::deserialize<api::CommandAck>(payload_for(fx.sink, ack_topic));
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "AC IEC: ISO verbs not applicable");
    }

    SECTION("ChargingPwmPaused: InjectFault -> Faulted") {
        auto ctx = fx.make_ctx();
        ChargingPwmPaused s{*ctx};
        Event ev{EventKind::InjectFault};
        ev.payload = api::InjectFaultParams{api::FaultType::RcdError, 30.0f};
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Faulted);
    }

    SECTION("ChargingPwmPaused: Disable -> Disabled") {
        auto ctx = fx.make_ctx();
        ChargingPwmPaused s{*ctx};
        auto result = s.feed(Event{EventKind::Disable});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Disabled);
    }

    SECTION("ChargingPwmPaused: QueryState publishes state") {
        auto ctx = fx.make_ctx();
        ChargingPwmPaused s{*ctx};
        fx.sink.clear();
        auto result = s.feed(Event{EventKind::QueryState});
        CHECK(result.new_state == nullptr);
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::ChargingPwmPaused);
    }

    // ---- Paused --------------------------------------------------------

    SECTION("Paused.enter sets CP=B, power off, calls iso_pause, arms 1h timer") {
        auto ctx = fx.make_ctx();
        Paused s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=false)"));
        CHECK(contains_substr(fx.mocks.iso.records, "pause_charging()"));
        REQUIRE_FALSE(fx.timer.state_timer_arms.empty());
        CHECK(fx.timer.state_timer_arms.back() == std::chrono::hours(1));
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Paused);
    }

    SECTION("Paused: ResumeSession seeds bcb_remaining=6 and -> BcbToggling") {
        auto ctx = fx.make_ctx();
        Paused s{*ctx};
        auto result = s.feed(Event{EventKind::ResumeSession});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::BcbToggling);
        CHECK(ctx->vars.bcb_remaining == 6);
    }

    SECTION("Paused: Unplug -> Stopping") {
        auto ctx = fx.make_ctx();
        Paused s{*ctx};
        auto result = s.feed(Event{EventKind::Unplug});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Stopping);
    }

    SECTION("Paused: StateDeadline -> Stopping") {
        auto ctx = fx.make_ctx();
        Paused s{*ctx};
        auto result = s.feed(Event{EventKind::StateDeadline});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Stopping);
    }

    SECTION("Paused: BspEvent Disconnected -> Unplugged") {
        auto ctx = fx.make_ctx();
        Paused s{*ctx};
        Event ev{EventKind::BspEvent};
        BspEventPayload p;
        p.bsp_event.event = ::types::board_support_common::Event::Disconnected;
        ev.payload = p;
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("Paused: InjectFault -> Faulted") {
        auto ctx = fx.make_ctx();
        Paused s{*ctx};
        Event ev{EventKind::InjectFault};
        ev.payload = api::InjectFaultParams{api::FaultType::CpErrorE, std::nullopt};
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Faulted);
    }

    SECTION("Paused: Disable -> Disabled") {
        auto ctx = fx.make_ctx();
        Paused s{*ctx};
        auto result = s.feed(Event{EventKind::Disable});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Disabled);
    }

    SECTION("Paused: QueryState publishes Paused") {
        auto ctx = fx.make_ctx();
        Paused s{*ctx};
        fx.sink.clear();
        auto result = s.feed(Event{EventKind::QueryState});
        CHECK(result.new_state == nullptr);
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Paused);
    }

    SECTION("Paused: StartSession rejected (session paused)") {
        auto ctx = fx.make_ctx();
        Paused s{*ctx};
        auto result = s.feed(Event{EventKind::StartSession});
        CHECK(result.new_state == nullptr);
        auto ack = api::deserialize<api::CommandAck>(payload_for(fx.sink, ack_topic));
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "session paused");
    }

    // ---- Stopping ------------------------------------------------------

    SECTION("Stopping.enter calls iso_stop, power off, CP=B, arms 10s timer") {
        auto ctx = fx.make_ctx();
        Stopping s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.iso.records, "stop_charging()"));
        CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=false)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=B)"));
        REQUIRE_FALSE(fx.timer.state_timer_arms.empty());
        CHECK(fx.timer.state_timer_arms.back() == std::chrono::seconds(10));
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Stopping);
    }

    SECTION("Stopping: IsoV2GFinished -> Unplugged") {
        auto ctx = fx.make_ctx();
        Stopping s{*ctx};
        auto result = s.feed(Event{EventKind::IsoV2GFinished});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("Stopping: StateDeadline -> Unplugged (forced)") {
        auto ctx = fx.make_ctx();
        Stopping s{*ctx};
        auto result = s.feed(Event{EventKind::StateDeadline});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("Stopping: BspEvent Disconnected -> Unplugged") {
        auto ctx = fx.make_ctx();
        Stopping s{*ctx};
        Event ev{EventKind::BspEvent};
        BspEventPayload p;
        p.bsp_event.event = ::types::board_support_common::Event::Disconnected;
        ev.payload = p;
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("Stopping: InjectFault -> Faulted") {
        auto ctx = fx.make_ctx();
        Stopping s{*ctx};
        Event ev{EventKind::InjectFault};
        ev.payload = api::InjectFaultParams{api::FaultType::V2GTimeout, std::nullopt};
        auto result = s.feed(ev);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Faulted);
    }

    SECTION("Stopping: Disable -> Disabled") {
        auto ctx = fx.make_ctx();
        Stopping s{*ctx};
        auto result = s.feed(Event{EventKind::Disable});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Disabled);
    }

    SECTION("Stopping: QueryState publishes Stopping") {
        auto ctx = fx.make_ctx();
        Stopping s{*ctx};
        fx.sink.clear();
        auto result = s.feed(Event{EventKind::QueryState});
        CHECK(result.new_state == nullptr);
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Stopping);
    }

    SECTION("Stopping: StartSession rejected (session stopping)") {
        auto ctx = fx.make_ctx();
        Stopping s{*ctx};
        auto result = s.feed(Event{EventKind::StartSession});
        CHECK(result.new_state == nullptr);
        auto ack = api::deserialize<api::CommandAck>(payload_for(fx.sink, ack_topic));
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "session stopping");
    }

    // ---- Faulted -------------------------------------------------------

    SECTION("Faulted.enter with DiodeFail records bsp_diode_fail(true) + publishes fault+state") {
        auto ctx = fx.make_ctx();
        ctx->vars.last_fault = api::FaultReport{api::FaultType::DiodeFail, std::nullopt, std::nullopt};
        Faulted s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.bsp.records, "diode_fail(value=true)"));
        REQUIRE(topic_recorded(fx.sink, fault_topic));
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Faulted);
    }

    SECTION("Faulted.leave with DiodeFail records bsp_diode_fail(false)") {
        auto ctx = fx.make_ctx();
        ctx->vars.last_fault = api::FaultReport{api::FaultType::DiodeFail, std::nullopt, std::nullopt};
        Faulted s{*ctx};
        s.enter();
        fx.mocks.bsp.clear();
        s.leave();
        CHECK(contains_substr(fx.mocks.bsp.records, "diode_fail(value=false)"));
        CHECK(fx.timer.state_timer_cancels == 1);
    }

    SECTION("Faulted.enter with RcdError records bsp_set_rcd_error(<mA>)") {
        auto ctx = fx.make_ctx();
        ctx->vars.last_fault = api::FaultReport{api::FaultType::RcdError, std::nullopt, 42.0f};
        Faulted s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.bsp.records, "set_rcd_error(rcd_current_mA=42)"));
    }

    SECTION("Faulted.enter with RcdError missing rcd_mA defaults to 0") {
        auto ctx = fx.make_ctx();
        ctx->vars.last_fault = api::FaultReport{api::FaultType::RcdError, std::nullopt, std::nullopt};
        Faulted s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.bsp.records, "set_rcd_error(rcd_current_mA=0)"));
    }

    SECTION("Faulted.leave with RcdError records bsp_set_rcd_error(0)") {
        auto ctx = fx.make_ctx();
        ctx->vars.last_fault = api::FaultReport{api::FaultType::RcdError, std::nullopt, 42.0f};
        Faulted s{*ctx};
        s.enter();
        fx.mocks.bsp.clear();
        s.leave();
        CHECK(contains_substr(fx.mocks.bsp.records, "set_rcd_error(rcd_current_mA=0)"));
    }

    SECTION("Faulted.enter with CpErrorE sets CP=E, no BSP fault dispatch") {
        auto ctx = fx.make_ctx();
        ctx->vars.last_fault = api::FaultReport{api::FaultType::CpErrorE, std::nullopt, std::nullopt};
        Faulted s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=E)"));
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "diode_fail"));
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_rcd_error"));
    }

    SECTION("Faulted.leave with CpErrorE is a no-op on BSP") {
        auto ctx = fx.make_ctx();
        ctx->vars.last_fault = api::FaultReport{api::FaultType::CpErrorE, std::nullopt, std::nullopt};
        Faulted s{*ctx};
        s.enter();
        fx.mocks.bsp.clear();
        s.leave();
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "diode_fail"));
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_rcd_error"));
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_cp_state"));
    }

    SECTION("Faulted.enter with SlacTimeout records NO BSP calls") {
        auto ctx = fx.make_ctx();
        ctx->vars.last_fault = api::FaultReport{api::FaultType::SlacTimeout, std::nullopt, std::nullopt};
        Faulted s{*ctx};
        s.enter();
        CHECK(fx.mocks.bsp.records.empty());
        REQUIRE(topic_recorded(fx.sink, fault_topic));
    }

    SECTION("Faulted.enter with V2GTimeout records NO BSP calls") {
        auto ctx = fx.make_ctx();
        ctx->vars.last_fault = api::FaultReport{api::FaultType::V2GTimeout, std::nullopt, std::nullopt};
        Faulted s{*ctx};
        s.enter();
        CHECK(fx.mocks.bsp.records.empty());
    }

    SECTION("Faulted.enter with Internal records NO BSP calls") {
        auto ctx = fx.make_ctx();
        ctx->vars.last_fault = api::FaultReport{api::FaultType::Internal, std::nullopt, std::nullopt};
        Faulted s{*ctx};
        s.enter();
        CHECK(fx.mocks.bsp.records.empty());
    }

    SECTION("Faulted.enter without last_fault emits Internal fault+state Faulted") {
        auto ctx = fx.make_ctx();
        ctx->vars.last_fault.reset();
        Faulted s{*ctx};
        s.enter();
        REQUIRE(topic_recorded(fx.sink, fault_topic));
        auto fr = api::deserialize<api::FaultReport>(payload_for(fx.sink, fault_topic));
        CHECK(fr.type == api::FaultType::Internal);
        REQUIRE(fr.message.has_value());
        CHECK(*fr.message == "last_fault unset");
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Faulted);
    }

    SECTION("Faulted: ClearFault -> Unplugged") {
        auto ctx = fx.make_ctx();
        ctx->vars.last_fault = api::FaultReport{api::FaultType::CpErrorE, std::nullopt, std::nullopt};
        Faulted s{*ctx};
        auto result = s.feed(Event{EventKind::ClearFault});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("Faulted: Unplug -> Unplugged") {
        auto ctx = fx.make_ctx();
        ctx->vars.last_fault = api::FaultReport{api::FaultType::DiodeFail, std::nullopt, std::nullopt};
        Faulted s{*ctx};
        auto result = s.feed(Event{EventKind::Unplug});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("Faulted: other events are unhandled") {
        auto ctx = fx.make_ctx();
        ctx->vars.last_fault = api::FaultReport{api::FaultType::DiodeFail, std::nullopt, std::nullopt};
        Faulted s{*ctx};
        auto result = s.feed(Event{EventKind::StartSession});
        CHECK(result.new_state == nullptr);
        CHECK(result.unhandled == true);
    }
}
