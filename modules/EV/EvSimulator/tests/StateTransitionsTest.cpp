// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/EventDispatch.hpp"
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
#include <everest/util/fsm/fsm.hpp>
#include <everest_api_types/ev_simulator/codec.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <variant>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_simulator;

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
        set_mode(*ctx, api::ChargeMode::AcIso2);
        ctx->vars.last_fault = api::FaultReport{api::FaultType::DiodeFail, std::nullopt, std::nullopt};
        ctx->mark_plugged_in(true);

        auto result = transition_to_disabled(*ctx);

        // BSP fault state cleared.
        CHECK(contains_substr(fx.mocks.bsp.records, "diode_fail(value=false)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "set_rcd_error(rcd_current_mA=0)"));
        // CP=A and power off.
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=A)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=false)"));
        // BSP disabled.
        CHECK(contains_substr(fx.mocks.bsp.records, "enable(value=false)"));
        // ISO stop invoked.
        CHECK(contains_substr(fx.mocks.iso.records, "stop_charging()"));
        // SimVars resets.
        CHECK_FALSE(ctx->vars.charge_mode().has_value());
        CHECK_FALSE(ctx->vars.last_fault.has_value());
        // Persisted.plugged_in cleared and KVS save invoked.
        CHECK(ctx->persisted_state().plugged_in == false);
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

    SECTION("set_desired_ac_params records calls and updates SimVars") {
        auto ctx = fx.make_ctx();
        ctx->set_desired_ac_params(16.0f, true);
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
        // A rejected start does not open a V2G session, so the resume-gate flag
        // must stay false.
        CHECK_FALSE(ctx->vars.iso_session_active);
    }

    SECTION("iso_start_charging(AcIso2, three_phases=true) maps to AC_three_phase_core") {
        auto ctx = fx.make_ctx();
        ctx->vars.three_phases = true;
        auto ok = ctx->iso_start_charging(api::ChargeMode::AcIso2, api::PaymentOption::ExternalPayment, 100, 5000);
        CHECK(ok == true); // MockIso15118Ev defaults next_start_charging_result=true
        CHECK(contains_substr(fx.mocks.iso.records, "mode=AC_three_phase_core"));
        CHECK(contains_substr(fx.mocks.iso.records, "payment=ExternalPayment"));
        // A successful start marks the V2G session live for the resume gate.
        CHECK(ctx->vars.iso_session_active);
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

    SECTION("iso_start_charging(AcIsoD20, three_phases=true) maps to AC_three_phase_core") {
        auto ctx = fx.make_ctx();
        ctx->vars.three_phases = true;
        auto ok = ctx->iso_start_charging(api::ChargeMode::AcIsoD20, std::nullopt, 0, 0);
        CHECK(ok == true);
        CHECK(contains_substr(fx.mocks.iso.records, "mode=AC_three_phase_core"));
    }

    SECTION("iso_start_charging(AcIsoD20, three_phases=false) maps to AC_single_phase_core") {
        auto ctx = fx.make_ctx();
        ctx->vars.three_phases = false;
        auto ok = ctx->iso_start_charging(api::ChargeMode::AcIsoD20, std::nullopt, 0, 0);
        CHECK(ok == true);
        CHECK(contains_substr(fx.mocks.iso.records, "mode=AC_single_phase_core"));
    }

    SECTION("iso_start_charging(DcIsoD20) maps to DC") {
        auto ctx = fx.make_ctx();
        auto ok = ctx->iso_start_charging(api::ChargeMode::DcIsoD20, std::nullopt, 0, 0);
        CHECK(ok == true);
        CHECK(contains_substr(fx.mocks.iso.records, "mode=DC,"));
    }

    SECTION("iso_start_charging passes Contract through") {
        auto ctx = fx.make_ctx();
        ctx->iso_start_charging(api::ChargeMode::AcIso2, api::PaymentOption::Contract, 0, 0);
        CHECK(contains_substr(fx.mocks.iso.records, "payment=Contract"));
    }

    SECTION("iso_start_charging(DcIsoD20) with bpt calls enable_sae then set_bpt_dc_params then start_charging") {
        auto ctx = fx.make_ctx();
        api::BptParams bpt{};
        bpt.discharge_max_current_limit = 32.0f;
        bpt.discharge_max_power_limit = 11000.0f;
        bpt.discharge_target_current = 16.0f;
        bpt.discharge_minimal_soc = 20.0f;
        ensure_session(*ctx).bpt = bpt;

        auto ok = ctx->iso_start_charging(api::ChargeMode::DcIsoD20, std::nullopt, 0, 0);
        CHECK(ok == true);

        auto enable_idx = index_of_substr(fx.mocks.iso.records, "enable_sae_j2847_v2g_v2h");
        auto set_bpt_idx = index_of_substr(fx.mocks.iso.records, "set_bpt_dc_params");
        auto start_idx = index_of_substr(fx.mocks.iso.records, "start_charging(mode=DC_BPT,");
        REQUIRE(enable_idx >= 0);
        REQUIRE(set_bpt_idx >= 0);
        REQUIRE(start_idx >= 0);
        CHECK(enable_idx < set_bpt_idx);
        CHECK(set_bpt_idx < start_idx);
    }

    SECTION("iso_start_charging(DcIso2) sets DC params from cfg with non-zero target current") {
        fx.cfg.dc_target_current = 5;
        fx.cfg.dc_target_voltage = 200;
        fx.cfg.dc_max_current_limit = 300;
        fx.cfg.dc_max_power_limit = 60000;
        fx.cfg.dc_max_voltage_limit = 500;
        fx.cfg.dc_energy_capacity = 60000;
        auto ctx = fx.make_ctx();

        auto ok = ctx->iso_start_charging(api::ChargeMode::DcIso2, std::nullopt, 0, 0);
        CHECK(ok == true);
        REQUIRE(index_of_substr(fx.mocks.iso.records, "set_dc_params(target_current=5") >= 0);
        CHECK(contains_substr(fx.mocks.iso.records, "target_voltage=200"));
        CHECK(contains_substr(fx.mocks.iso.records, "max_current_limit=300"));
    }

    SECTION("iso_start_charging(DcIsoD20) sets DC params from cfg with non-zero target current") {
        fx.cfg.dc_target_current = 5;
        fx.cfg.dc_target_voltage = 200;
        fx.cfg.dc_max_current_limit = 300;
        fx.cfg.dc_max_power_limit = 60000;
        fx.cfg.dc_max_voltage_limit = 500;
        fx.cfg.dc_energy_capacity = 60000;
        auto ctx = fx.make_ctx();

        auto ok = ctx->iso_start_charging(api::ChargeMode::DcIsoD20, std::nullopt, 0, 0);
        CHECK(ok == true);
        REQUIRE(index_of_substr(fx.mocks.iso.records, "set_dc_params(target_current=5") >= 0);
        CHECK(contains_substr(fx.mocks.iso.records, "target_voltage=200"));
        CHECK(contains_substr(fx.mocks.iso.records, "max_current_limit=300"));
    }

    SECTION("iso_start_charging(AcIsoD20) with bpt selects AC_BPT etm") {
        auto ctx = fx.make_ctx();
        api::BptParams bpt{};
        bpt.discharge_max_current_limit = 16.0f;
        bpt.discharge_max_power_limit = 7400.0f;
        bpt.discharge_target_current = 8.0f;
        bpt.discharge_minimal_soc = 25.0f;
        ensure_session(*ctx).bpt = bpt;

        auto ok = ctx->iso_start_charging(api::ChargeMode::AcIsoD20, std::nullopt, 0, 0);
        CHECK(ok == true);

        CHECK(contains_substr(fx.mocks.iso.records, "start_charging(mode=AC_BPT,"));
        CHECK(contains_substr(fx.mocks.iso.records, "enable_sae_j2847_v2g_v2h"));
        CHECK(contains_substr(fx.mocks.iso.records, "set_bpt_dc_params"));
    }

    SECTION("iso_start_charging(DcIsoD20) without bpt does NOT call BPT actions") {
        auto ctx = fx.make_ctx();
        ensure_session(*ctx).bpt.reset();

        auto ok = ctx->iso_start_charging(api::ChargeMode::DcIsoD20, std::nullopt, 0, 0);
        CHECK(ok == true);
        CHECK(contains_substr(fx.mocks.iso.records, "start_charging(mode=DC,"));
        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "enable_sae_j2847"));
        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "set_bpt_dc_params"));
    }

    SECTION("iso_start_charging(AcIso2) ignores bpt setting (no BPT pre-calls, etm unchanged)") {
        auto ctx = fx.make_ctx();
        ctx->vars.three_phases = true;
        api::BptParams bpt{};
        bpt.discharge_max_current_limit = 32.0f;
        bpt.discharge_max_power_limit = 11000.0f;
        bpt.discharge_target_current = 16.0f;
        bpt.discharge_minimal_soc = 20.0f;
        ensure_session(*ctx).bpt = bpt;

        auto ok = ctx->iso_start_charging(api::ChargeMode::AcIso2, std::nullopt, 0, 0);
        CHECK(ok == true);
        // bpt is harmless metadata on non-D20 modes: etm stays AC_three_phase_core
        // and no BPT pre-calls fire.
        CHECK(contains_substr(fx.mocks.iso.records, "start_charging(mode=AC_three_phase_core,"));
        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "enable_sae_j2847"));
        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "set_bpt_dc_params"));
    }

    SECTION("iso_start_charging(DcIsoD20) with mcs selects MCS etm") {
        auto ctx = fx.make_ctx();
        ensure_session(*ctx).mcs_enabled = true;

        auto ok = ctx->iso_start_charging(api::ChargeMode::DcIsoD20, std::nullopt, 0, 0);
        CHECK(ok == true);
        CHECK(contains_substr(fx.mocks.iso.records, "start_charging(mode=MCS,"));
        // MCS without BPT does not fire BPT pre-calls.
        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "enable_sae_j2847"));
        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "set_bpt_dc_params"));
    }

    SECTION("iso_start_charging(DcIsoD20) with mcs + bpt selects MCS_BPT etm") {
        auto ctx = fx.make_ctx();
        api::BptParams bpt{};
        bpt.discharge_max_current_limit = 32.0f;
        bpt.discharge_max_power_limit = 11000.0f;
        bpt.discharge_target_current = 16.0f;
        bpt.discharge_minimal_soc = 20.0f;
        ensure_session(*ctx).bpt = bpt;
        ensure_session(*ctx).mcs_enabled = true;

        auto ok = ctx->iso_start_charging(api::ChargeMode::DcIsoD20, std::nullopt, 0, 0);
        CHECK(ok == true);

        auto enable_idx = index_of_substr(fx.mocks.iso.records, "enable_sae_j2847_v2g_v2h");
        auto set_bpt_idx = index_of_substr(fx.mocks.iso.records, "set_bpt_dc_params");
        auto start_idx = index_of_substr(fx.mocks.iso.records, "start_charging(mode=MCS_BPT,");
        REQUIRE(enable_idx >= 0);
        REQUIRE(set_bpt_idx >= 0);
        REQUIRE(start_idx >= 0);
        CHECK(enable_idx < set_bpt_idx);
        CHECK(set_bpt_idx < start_idx);
    }

    SECTION("iso_start_charging returns false when the wired peer rejects the start") {
        // The peer is present and wired, but the ISO15118_ev side answers the
        // start_charging request negatively. iso_start_charging must propagate
        // that false back to the caller (V2GNegotiating routes it to Faulted).
        auto ctx = fx.make_ctx();
        fx.mocks.iso.next_start_charging_result = false;

        auto ok = ctx->iso_start_charging(api::ChargeMode::DcIso2, std::nullopt, 0, 0);

        CHECK(ok == false);
        // The call still reached the peer (the rejection came from the peer,
        // not from a short-circuit), so the record is present.
        CHECK(contains_substr(fx.mocks.iso.records, "start_charging(mode=DC_extended,"));
    }

    SECTION("iso_start_charging false propagates for an AC mode too") {
        auto ctx = fx.make_ctx();
        ctx->vars.three_phases = true;
        fx.mocks.iso.next_start_charging_result = false;

        auto ok = ctx->iso_start_charging(api::ChargeMode::AcIso2, std::nullopt, 0, 0);

        CHECK(ok == false);
        CHECK(contains_substr(fx.mocks.iso.records, "start_charging(mode=AC_three_phase_core,"));
    }

    SECTION("iso_start_charging forwards departure_time_s and e_amount_wh to the peer") {
        auto ctx = fx.make_ctx();
        ctx->iso_start_charging(api::ChargeMode::DcIso2, std::nullopt, 7200, 25000);
        CHECK(contains_substr(fx.mocks.iso.records, "departure_time=7200"));
        CHECK(contains_substr(fx.mocks.iso.records, "e_amount=25000"));
    }

    SECTION("iso_start_charging forwards zeroed departure_time_s and e_amount_wh verbatim") {
        auto ctx = fx.make_ctx();
        ctx->iso_start_charging(api::ChargeMode::AcIso2, std::nullopt, 0, 0);
        CHECK(contains_substr(fx.mocks.iso.records, "departure_time=0"));
        CHECK(contains_substr(fx.mocks.iso.records, "e_amount=0"));
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
        ctx->mark_plugged_in(true);
        ctx->remember_session_config(api::SessionConfigParams{api::AcIecSessionParams{20.0f, false, std::nullopt}});

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
        REQUIRE(roundtrip.configured_session.has_value());
        CHECK(api::mode_of(*roundtrip.configured_session) == api::ChargeMode::AcIec);
    }

    SECTION("kvs_save records NOTHING when keep_cross_boot_plugin_state=false") {
        fx.cfg.keep_cross_boot_plugin_state = false;
        auto ctx = fx.make_ctx();
        ctx->mark_plugged_in(true);

        ctx->kvs_save();

        CHECK(fx.mocks.kvs.records.empty());
    }

    SECTION("kvs_load with missing key leaves persisted at defaults") {
        fx.cfg.keep_cross_boot_plugin_state = true;
        fx.mocks.next_kvs_load_value = std::nullopt; // key not present
        auto ctx = fx.make_ctx();

        ctx->kvs_load();

        // defaults: plugged_in=false, configured_session=nullopt.
        CHECK(ctx->persisted_state().plugged_in == false);
        CHECK_FALSE(ctx->persisted_state().configured_session.has_value());
    }

    SECTION("kvs_load from valid JSON populates persisted and seeds configured_session") {
        fx.cfg.keep_cross_boot_plugin_state = true;
        PersistedState seed{};
        seed.plugged_in = true;
        seed.configured_session = api::SessionConfigParams{api::DcIso2SessionParams{}};
        fx.mocks.next_kvs_load_value = nlohmann::json(seed).dump();
        auto ctx = fx.make_ctx();

        ctx->kvs_load();

        CHECK(ctx->persisted_state().plugged_in == true);
        REQUIRE(ctx->persisted_state().configured_session.has_value());
        CHECK(api::mode_of(*ctx->persisted_state().configured_session) == api::ChargeMode::DcIso2);
        // kvs_load seeds the live latched config too.
        REQUIRE(ctx->configured_session.has_value());
        CHECK(api::mode_of(*ctx->configured_session) == api::ChargeMode::DcIso2);
    }

    SECTION("mutators round-trip through kvs_save then kvs_load") {
        fx.cfg.keep_cross_boot_plugin_state = true;
        auto saver = fx.make_ctx();
        saver->mark_plugged_in(true);
        saver->remember_session_config(api::SessionConfigParams{api::AcIso2SessionParams{}});
        saver->kvs_save();

        // Pull the JSON payload back out of the recorded store(...) call and
        // feed it to a fresh context's load path.
        REQUIRE_FALSE(fx.mocks.kvs.records.empty());
        const auto& rec = fx.mocks.kvs.records.back();
        const auto json_pos = rec.find(",json=");
        REQUIRE(json_pos != std::string::npos);
        std::string payload = rec.substr(json_pos + 6);
        REQUIRE_FALSE(payload.empty());
        payload.pop_back(); // drop trailing ')'

        fx.mocks.next_kvs_load_value = payload;
        auto loader = fx.make_ctx();
        loader->kvs_load();

        CHECK(loader->persisted_state().plugged_in == true);
        REQUIRE(loader->persisted_state().configured_session.has_value());
        CHECK(api::mode_of(*loader->persisted_state().configured_session) == api::ChargeMode::AcIso2);
        REQUIRE(loader->configured_session.has_value());
        CHECK(api::mode_of(*loader->configured_session) == api::ChargeMode::AcIso2);
    }

    SECTION("kvs_load tolerates a legacy last_mode blob without throwing") {
        // Pre-configure_session blobs carry `last_mode` (and possibly
        // `last_scenario`). Both keys are now unknown and must be silently
        // ignored: plugged_in still loads, configured_session defaults to
        // nullopt, no exception.
        fx.cfg.keep_cross_boot_plugin_state = true;
        nlohmann::json legacy = {
            {"plugged_in", true},
            {"last_mode", nlohmann::json::parse(api::serialize(api::ChargeMode::DcIso2))},
            {"last_scenario", nlohmann::json::parse(api::serialize(api::ScenarioName::DcIsoBasic))}};
        fx.mocks.next_kvs_load_value = legacy.dump();
        auto ctx = fx.make_ctx();

        ctx->kvs_load();

        CHECK(ctx->persisted_state().plugged_in == true);
        CHECK_FALSE(ctx->persisted_state().configured_session.has_value());
        CHECK_FALSE(ctx->configured_session.has_value());
    }

    SECTION("publish_e2m_state records topic+payload") {
        auto ctx = fx.make_ctx();
        ctx->publish_e2m_state(api::FsmState::Plugged);
        auto topic = fx.topics.everest_to_extern("state");
        REQUIRE(topic_recorded(fx.sink, topic));
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, topic));
        CHECK(decoded == api::FsmState::Plugged);
    }

    SECTION("publish_e2m_command_ack records Rejected ack on command_ack topic") {
        auto ctx = fx.make_ctx();
        ctx->publish_e2m_command_ack("configure_session", "no_session_active");
        auto topic = fx.topics.everest_to_extern("command_ack");
        REQUIRE(topic_recorded(fx.sink, topic));
        auto decoded = api::deserialize<api::CommandAck>(payload_for(fx.sink, topic));
        CHECK(decoded.command == "configure_session");
        CHECK(decoded.status == api::CommandAckStatus::Rejected);
        REQUIRE(decoded.reason.has_value());
        CHECK(*decoded.reason == "no_session_active");
    }

    SECTION("publish_e2m_fault records fault topic+payload") {
        auto ctx = fx.make_ctx();
        api::FaultReport f{api::FaultType::SlacTimeout, std::nullopt, std::nullopt};
        ctx->publish_e2m_fault(f);
        auto topic = fx.topics.everest_to_extern("fault");
        REQUIRE(topic_recorded(fx.sink, topic));
        auto decoded = api::deserialize<api::FaultReport>(payload_for(fx.sink, topic));
        CHECK(decoded.type == api::FaultType::SlacTimeout);
    }

    SECTION("publish_e2m_ev_info records ev_info with current soc") {
        auto ctx = fx.make_ctx();
        ctx->vars.soc_pct = 75.0f;
        ctx->publish_e2m_ev_info();
        auto topic = fx.topics.everest_to_extern("ev_info");
        REQUIRE(topic_recorded(fx.sink, topic));
        auto decoded = api::deserialize<api::EvInfo>(payload_for(fx.sink, topic));
        CHECK(decoded.soc_pct == 75.0f);
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
        set_mode(*ctx, api::ChargeMode::AcIso2);
        ctx->vars.last_fault = api::FaultReport{api::FaultType::RcdError, std::nullopt, std::nullopt};
        ctx->mark_plugged_in(true);
        Unplugged s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=A)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=false)"));
        // BSP enabled after clearing session state.
        CHECK(contains_substr(fx.mocks.bsp.records, "enable(value=true)"));
        CHECK(contains_substr(fx.mocks.iso.records, "stop_charging()"));
        CHECK_FALSE(ctx->vars.charge_mode().has_value());
        CHECK_FALSE(ctx->vars.last_fault.has_value());
        CHECK(ctx->persisted_state().plugged_in == false);
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Unplugged);
    }

    SECTION("Unplugged.enter clears slac_unmatched so it cannot leak across sessions") {
        auto ctx = fx.make_ctx();
        // A SLAC teardown latched in a prior session must not force a redundant
        // re-match on the first resume of the next session.
        ctx->vars.slac_unmatched = true;
        Unplugged s{*ctx};
        s.enter();
        CHECK_FALSE(ctx->vars.slac_unmatched);
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

    SECTION("Unplugged: SetSoc above 100 clamps to 100") {
        auto ctx = fx.make_ctx();
        Unplugged s{*ctx};
        Event ev{EventKind::SetSoc};
        ev.payload = api::SetSocParams{150.0f};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(ctx->vars.soc_pct == 100.0f);
        CHECK(ctx->vars.battery_charge_wh == ctx->vars.battery_capacity_wh);
        CHECK(ctx->vars.soc_pct <= 100.0f);
        CHECK(ctx->vars.soc_pct >= 0.0f);
    }

    SECTION("Unplugged: SetSoc below 0 clamps to 0") {
        auto ctx = fx.make_ctx();
        Unplugged s{*ctx};
        Event ev{EventKind::SetSoc};
        ev.payload = api::SetSocParams{-10.0f};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(ctx->vars.soc_pct == 0.0f);
        CHECK(ctx->vars.battery_charge_wh == 0.0f);
        CHECK(ctx->vars.soc_pct >= 0.0f);
        CHECK(ctx->vars.soc_pct <= 100.0f);
    }

    SECTION("Unplugged: SetSoc with extreme value does not publish out-of-range SoC") {
        auto ctx = fx.make_ctx();
        Unplugged s{*ctx};
        Event ev{EventKind::SetSoc};
        ev.payload = api::SetSocParams{1e30f};
        s.feed(ev);
        CHECK(ctx->vars.soc_pct == 100.0f);
        CHECK(ctx->vars.battery_charge_wh == ctx->vars.battery_capacity_wh);
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

    SECTION("Unplugged: QueryState publishes Unplugged, no transition") {
        auto ctx = fx.make_ctx();
        Unplugged s{*ctx};
        auto result = s.feed(Event{EventKind::QueryState});
        CHECK(result.new_state == nullptr);
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Unplugged);
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
        CHECK(ctx->persisted_state().plugged_in == true);
        CHECK(contains_substr(fx.mocks.kvs.records, "store(key=evsim_1_state"));
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::Plugged);
    }

    SECTION("Plugged: latched AcIec applies AC params, stays in Plugged") {
        auto ctx = fx.make_ctx();
        ctx->configured_session = api::SessionConfigParams{api::AcIecSessionParams{20.0f, false, std::nullopt}};
        Plugged s{*ctx};
        auto result = s.feed(Event{BeginSessionEvt{}});
        CHECK(result.new_state == nullptr);
        CHECK(ctx->vars.charge_mode() == api::ChargeMode::AcIec);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=20)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "set_three_phases(three_phases=false)"));
    }

    SECTION("Plugged: no latched config synthesizes AcIec from cfg defaults") {
        auto ctx = fx.make_ctx();
        REQUIRE_FALSE(ctx->configured_session.has_value());
        Plugged s{*ctx};
        auto result = s.feed(Event{BeginSessionEvt{}});
        CHECK(result.new_state == nullptr);
        CHECK(ctx->vars.charge_mode() == api::ChargeMode::AcIec);
        // cfg defaults: max_current_a=16, three_phases=true.
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=16)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "set_three_phases(three_phases=true)"));
    }

    SECTION("Plugged: latched AcIso2 -> SlacMatching") {
        auto ctx = fx.make_ctx();
        ctx->configured_session = api::SessionConfigParams{api::AcIso2SessionParams{}};
        Plugged s{*ctx};
        auto result = s.feed(Event{BeginSessionEvt{}});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::SlacMatching);
        CHECK(ctx->vars.charge_mode() == api::ChargeMode::AcIso2);
        // AC params applied for AcIso2 path
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current"));
    }

    SECTION("Plugged: latched DcIso2 -> SlacMatching, no AC params") {
        auto ctx = fx.make_ctx();
        ctx->configured_session = api::SessionConfigParams{api::DcIso2SessionParams{}};
        Plugged s{*ctx};
        auto result = s.feed(Event{BeginSessionEvt{}});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::SlacMatching);
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_ac_max_current"));
    }

    SECTION("Plugged: latched AcIsoD20 -> SlacMatching") {
        auto ctx = fx.make_ctx();
        ctx->configured_session = api::SessionConfigParams{api::AcIsoD20SessionParams{}};
        Plugged s{*ctx};
        auto result = s.feed(Event{BeginSessionEvt{}});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::SlacMatching);
        CHECK(ctx->vars.charge_mode() == api::ChargeMode::AcIsoD20);
        // AC params applied for AcIsoD20 like AcIso2
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current"));
    }

    SECTION("Plugged: latched config stashes payment on ctx.vars for V2GNegotiating") {
        auto ctx = fx.make_ctx();
        api::DcIso2SessionParams params{};
        params.payment = api::PaymentOption::Contract;
        ctx->configured_session = api::SessionConfigParams{params};
        Plugged s{*ctx};
        auto result = s.feed(Event{BeginSessionEvt{}});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::SlacMatching);
        REQUIRE(ctx->vars.payment().has_value());
        CHECK(*ctx->vars.payment() == api::PaymentOption::Contract);
    }

    SECTION("Plugged: BspMeasurement triggers Charging when AcIec PWM in range") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
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
        set_mode(*ctx, api::ChargeMode::AcIec);
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

    SECTION("Plugged: BspMeasurement in PWM window in AcIso2 does NOT transition") {
        // ISO modes must wait for V2G negotiation; the PWM-only path is
        // restricted to AcIec. Pins that a refactor loosening the guard
        // (e.g. dropping the charge_mode check) cannot regress ISO modes
        // into spuriously entering any state on PWM alone.
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIso2);
        Plugged s{*ctx};
        Event ev{EventKind::BspMeasurement};
        BspMeasurementPayload m;
        m.cp_pwm_duty_cycle = 50; // squarely inside the (7, 97) window
        m.rcd_current_mA = std::nullopt;
        ev.payload = m;
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(ctx->vars.pwm_duty_cycle == 50);
    }

    SECTION("Plugged: BspMeasurement in PWM window in AcIsoD20 does NOT transition") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIsoD20);
        Plugged s{*ctx};
        Event ev{EventKind::BspMeasurement};
        BspMeasurementPayload m;
        m.cp_pwm_duty_cycle = 50;
        m.rcd_current_mA = std::nullopt;
        ev.payload = m;
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(ctx->vars.pwm_duty_cycle == 50);
    }

    SECTION("Plugged: BspMeasurement in PWM window in DcIso2 does NOT transition") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        Plugged s{*ctx};
        Event ev{EventKind::BspMeasurement};
        BspMeasurementPayload m;
        m.cp_pwm_duty_cycle = 50;
        m.rcd_current_mA = std::nullopt;
        ev.payload = m;
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(ctx->vars.pwm_duty_cycle == 50);
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

    SECTION("SlacMatching.enter without slac peer: no last_fault pre-seed, "
            "InjectFault carries 'no ev_slac peer'") {
        auto ctx = fx.make_ctx();
        ctx->peer_actions.slac = {}; // peer absent
        SlacMatching s{*ctx};
        s.enter();

        // The fix carries the message on the event payload instead of
        // pre-seeding vars.last_fault, so an intervening transition cannot
        // leave it stale for a later Faulted entry.
        CHECK_FALSE(ctx->vars.last_fault.has_value());
        REQUIRE(fx.timer.enqueued_events.size() == 1);
        REQUIRE(kind_of(fx.timer.enqueued_events[0]) == EventKind::InjectFault);
        auto p = std::get<api::InjectFaultParams>(fx.timer.enqueued_events[0].payload);
        CHECK(p.type == api::FaultType::Internal);
        REQUIRE(p.message.has_value());
        CHECK(*p.message == "no ev_slac peer");

        // Feeding the carried InjectFault produces Faulted with the payload
        // message (transition_to_fault prefers p.message over last_fault).
        auto result = s.feed(fx.timer.enqueued_events[0]);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Faulted);
        REQUIRE(ctx->vars.last_fault.has_value());
        REQUIRE(ctx->vars.last_fault->message.has_value());
        CHECK(*ctx->vars.last_fault->message == "no ev_slac peer");
    }

    SECTION("SlacMatching.enter with wired slac peer returning false: "
            "message distinguishes a rejected call from an absent peer") {
        auto ctx = fx.make_ctx();
        fx.mocks.slac.next_trigger_matching_result = false; // wired, rejects
        SlacMatching s{*ctx};
        s.enter();

        CHECK(contains_substr(fx.mocks.slac.records, "trigger_matching()"));
        CHECK_FALSE(ctx->vars.last_fault.has_value());
        REQUIRE(fx.timer.enqueued_events.size() == 1);
        auto p = std::get<api::InjectFaultParams>(fx.timer.enqueued_events[0].payload);
        REQUIRE(p.message.has_value());
        CHECK(*p.message == "ev_slac trigger_matching rejected");
        CHECK(*p.message != "no ev_slac peer");
    }

    SECTION("V2GNegotiating.enter on iso_start_charging rejection: no pre-seed, "
            "InjectFault carries 'iso_start_charging rejected'") {
        auto ctx = fx.make_ctx();
        ctx->peer_actions.iso = {}; // iso peer absent -> iso_start_charging false
        set_mode(*ctx, api::ChargeMode::DcIso2);
        V2GNegotiating s{*ctx};
        s.enter();

        CHECK_FALSE(ctx->vars.last_fault.has_value());
        REQUIRE(fx.timer.enqueued_events.size() == 1);
        REQUIRE(kind_of(fx.timer.enqueued_events[0]) == EventKind::InjectFault);
        auto p = std::get<api::InjectFaultParams>(fx.timer.enqueued_events[0].payload);
        CHECK(p.type == api::FaultType::Internal);
        REQUIRE(p.message.has_value());
        CHECK(*p.message == "iso_start_charging rejected");
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
        set_mode(*ctx, api::ChargeMode::AcIso2);
        ctx->vars.three_phases = true;
        V2GNegotiating s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.iso.records, "start_charging"));
        REQUIRE(fx.timer.state_timer_arms.size() == 1);
        CHECK(fx.timer.state_timer_arms[0] == std::chrono::seconds(60));
        auto decoded = api::deserialize<api::FsmState>(payload_for(fx.sink, state_topic));
        CHECK(decoded == api::FsmState::V2GNegotiating);
    }

    SECTION("V2GNegotiating.enter forwards ctx.vars.payment=Contract to iso_start_charging") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ensure_session(*ctx).payment = api::PaymentOption::Contract;
        V2GNegotiating s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.iso.records, "payment=Contract"));
    }

    SECTION("V2GNegotiating.enter forwards ctx.vars.payment=ExternalPayment to iso_start_charging") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ensure_session(*ctx).payment = api::PaymentOption::ExternalPayment;
        REQUIRE(ctx->vars.payment().has_value());
        CHECK(*ctx->vars.payment() == api::PaymentOption::ExternalPayment);
        V2GNegotiating s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.iso.records, "payment=ExternalPayment"));
    }

    SECTION("V2GNegotiating.enter with unset payment defaults to ExternalPayment") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ensure_session(*ctx).payment.reset();
        CHECK_FALSE(ctx->vars.payment().has_value());
        V2GNegotiating s{*ctx};
        s.enter();
        CHECK(contains_substr(fx.mocks.iso.records, "payment=ExternalPayment"));
    }

    SECTION("V2GNegotiating.enter skips iso_start when charge_mode missing") {
        auto ctx = fx.make_ctx();
        clear_session(*ctx);
        V2GNegotiating s{*ctx};
        s.enter();
        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "start_charging"));
        REQUIRE(fx.timer.state_timer_arms.size() == 1);
        CHECK(fx.timer.state_timer_arms[0] == std::chrono::seconds(60));
    }

    SECTION("V2GNegotiating: IsoPowerReady in AcIso2 -> Charging") {
        // AC ISO has no dc_power_on milestone, so ev_power_ready is the
        // charge-loop entry gate. Mirrors EvManager's AC path.
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIso2);
        V2GNegotiating s{*ctx};
        auto result = s.feed(Event{EventKind::IsoPowerReady});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Charging);
    }

    SECTION("V2GNegotiating: IsoPowerReady in AcIsoD20 -> Charging") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIsoD20);
        V2GNegotiating s{*ctx};
        auto result = s.feed(Event{EventKind::IsoPowerReady});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Charging);
    }

    SECTION("V2GNegotiating: IsoPowerReady in DcIso2 asserts CP=C and STAYS (not Charging)") {
        // DC ISO splits the charge-loop entry into two milestones, mirroring
        // EvManager: ev_power_ready (ISO_POWER_READY) asserts CP=C and holds
        // through CableCheck/PreCharge; only dc_power_on enters Charging.
        // Transitioning to Charging here is premature -- it signals "Charging"
        // while the SECC is still entering CableCheck and pausing then drops
        // CP to B mid-CableCheck, killing the PSU.
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        V2GNegotiating s{*ctx};
        auto result = s.feed(Event{EventKind::IsoPowerReady});
        CHECK(result.unhandled == false);
        CHECK(result.new_state == nullptr);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=C)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=true)"));
    }

    SECTION("V2GNegotiating: IsoPowerReady in DcIsoD20 asserts CP=C and STAYS (not Charging)") {
        // DcIsoD20 also publishes dc_power_on (iso15118_20_states.py DCPreCharge),
        // so it follows the same two-milestone gate as DcIso2.
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIsoD20);
        V2GNegotiating s{*ctx};
        auto result = s.feed(Event{EventKind::IsoPowerReady});
        CHECK(result.unhandled == false);
        CHECK(result.new_state == nullptr);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=C)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=true)"));
    }

    SECTION("V2GNegotiating: IsoDcPowerOn in DcIso2 -> Charging") {
        // dc_power_on (PreCharge complete) is the DC charging milestone.
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        V2GNegotiating s{*ctx};
        auto result = s.feed(Event{EventKind::IsoDcPowerOn});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Charging);
    }

    SECTION("V2GNegotiating: IsoDcPowerOn in DcIsoD20 -> Charging") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIsoD20);
        V2GNegotiating s{*ctx};
        auto result = s.feed(Event{EventKind::IsoDcPowerOn});
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
        set_mode(*ctx, api::ChargeMode::AcIec);
        Charging s{*ctx};
        auto result = s.feed(Event{EventKind::StopSession});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("Charging: StopSession in AcIso2 -> Stopping") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIso2);
        Charging s{*ctx};
        auto result = s.feed(Event{EventKind::StopSession});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Stopping);
    }

    SECTION("Charging: StopSession in DcIso2 -> Stopping") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
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
        set_mode(*ctx, api::ChargeMode::AcIec);
        Charging s{*ctx};
        Event ev{EventKind::SetChargingCurrent};
        ev.payload = api::SetChargingCurrentParams{12.5f, false};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=12.5)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "set_three_phases(three_phases=false)"));
    }

    SECTION("Charging: SetChargingCurrent without ramp_ms applies instantly") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        ctx->vars.charging_current_a = 6.0f;
        Charging s{*ctx};
        Event ev{EventKind::SetChargingCurrent};
        ev.payload = api::SetChargingCurrentParams{20.0f, true, std::nullopt};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=20)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "set_three_phases(three_phases=true)"));
        CHECK_FALSE(ctx->vars.active_ramp.has_value());
        CHECK(ctx->vars.charging_current_a == 20.0f);
    }

    SECTION("Charging: SetChargingCurrent with ramp_ms captures ActiveRamp and defers BSP apply") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        ctx->vars.charging_current_a = 6.0f;
        ctx->vars.three_phases = true;
        Charging s{*ctx};
        Event ev{EventKind::SetChargingCurrent};
        ev.payload = api::SetChargingCurrentParams{16.0f, false, 2000};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        // Capture must NOT touch the BSP immediately; the tick handler drives
        // the ramp.
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_ac_max_current"));
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_three_phases"));
        REQUIRE(ctx->vars.active_ramp.has_value());
        const auto& r = *ctx->vars.active_ramp;
        CHECK(r.start_a == 6.0f);
        CHECK(r.target_a == 16.0f);
        CHECK(r.three_phases == false);
        CHECK(r.end_at - r.start_at == std::chrono::milliseconds{2000});
    }

    SECTION("Charging: SetChargingCurrent with ramp_ms=0 applies instantly") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        Charging s{*ctx};
        Event ev{EventKind::SetChargingCurrent};
        ev.payload = api::SetChargingCurrentParams{8.0f, true, 0};
        auto result = s.feed(ev);
        CHECK(result.new_state == nullptr);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=8)"));
        CHECK_FALSE(ctx->vars.active_ramp.has_value());
    }

    SECTION("Charging::leave clears active_ramp") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        Charging s{*ctx};
        s.enter();
        ActiveRamp r;
        r.start_a = 6.0f;
        r.target_a = 16.0f;
        r.three_phases = true;
        r.start_at = std::chrono::steady_clock::now();
        r.end_at = r.start_at + std::chrono::milliseconds{2000};
        ctx->vars.active_ramp = r;
        s.leave();
        CHECK_FALSE(ctx->vars.active_ramp.has_value());
    }

    SECTION("Charging: SetChargingCurrent in DcIso2 rejected with DC reason") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
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
        set_mode(*ctx, api::ChargeMode::AcIec);
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
        set_mode(*ctx, api::ChargeMode::AcIec);
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
        set_mode(*ctx, api::ChargeMode::AcIec);
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
        set_mode(*ctx, api::ChargeMode::DcIso2);
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
        set_mode(*ctx, api::ChargeMode::AcIec);
        Charging s{*ctx};
        auto result = s.feed(Event{EventKind::Unplug});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Unplugged);
    }

    SECTION("Charging: Unplug in DcIso2 -> Stopping") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
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

    SECTION("Charging: DC present-current / present-voltage are var-only no-ops, no Fault") {
        // DC charging lives in Charging, and the SECC streams
        // dc_evse_present_current / dc_evse_present_voltage straight through the
        // dispatch loop (EvSimRuntime feeds them via feed_with_fault_isolation).
        // These carry a DcEvsePresentCurrentPayload / DcEvsePresentVoltagePayload,
        // not an IsoAcMaxCurrentEvt: the Charging handler must treat both as pure
        // no-ops and never std::get the wrong variant alternative. A wrong-arm
        // grouping throws std::bad_variant_access, which the loop converts into a
        // forced internal Fault -- the regression this guards against.
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        std::unique_ptr<fsm::v2::FSM<StateBase>> fsm =
            std::make_unique<fsm::v2::FSM<StateBase>>(std::make_unique<Charging>(*ctx));
        REQUIRE(fsm->get_current_state_id() == api::FsmState::Charging);

        Event present_current{DcEvsePresentCurrentPayload{125.0}};
        REQUIRE_NOTHROW(feed_with_fault_isolation(fsm, *ctx, present_current));
        CHECK(fsm->get_current_state_id() == api::FsmState::Charging);
        CHECK(fsm->get_current_state_id() != api::FsmState::Faulted);

        Event present_voltage{DcEvsePresentVoltagePayload{400.0}};
        REQUIRE_NOTHROW(feed_with_fault_isolation(fsm, *ctx, present_voltage));
        CHECK(fsm->get_current_state_id() == api::FsmState::Charging);
        CHECK(fsm->get_current_state_id() != api::FsmState::Faulted);

        // The no-op must not have touched the BSP AC-current rail (the wrong-arm
        // body would have re-applied set_ac_max_current).
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_ac_max_current"));
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
        set_mode(*ctx, api::ChargeMode::AcIec);
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
        auto result = s.feed(Event{EventKind::ConfigureSession});
        CHECK(result.new_state == nullptr);
        CHECK(result.unhandled == true);
    }
}

TEST_CASE("EvSimulator group3 transitions", "[evsim][group3]") {
    TestFixture fx;
    const auto ack_topic = fx.topics.everest_to_extern("command_ack");

    // ---- Plugged + D20 -------------------------------------------------

    SECTION("Plugged: latched AcIsoD20{three_phases=true} -> SlacMatching") {
        auto ctx = fx.make_ctx();
        api::AcIsoD20SessionParams d20{};
        d20.charging_current_a = 16.0f;
        d20.three_phases = true;
        ctx->configured_session = api::SessionConfigParams{d20};
        Plugged s{*ctx};
        auto result = s.feed(Event{BeginSessionEvt{}});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::SlacMatching);
        CHECK(ctx->vars.charge_mode() == api::ChargeMode::AcIsoD20);
        // No reject ack should have been recorded.
        CHECK_FALSE(topic_recorded(fx.sink, ack_topic));
    }

    SECTION("Plugged: latched DcIsoD20 -> SlacMatching") {
        auto ctx = fx.make_ctx();
        ctx->configured_session = api::SessionConfigParams{api::DcIsoD20SessionParams{}};
        Plugged s{*ctx};
        auto result = s.feed(Event{BeginSessionEvt{}});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::SlacMatching);
        CHECK(ctx->vars.charge_mode() == api::ChargeMode::DcIsoD20);
        // DC path: no AC params applied.
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "set_ac_max_current"));
        CHECK_FALSE(topic_recorded(fx.sink, ack_topic));
    }

    SECTION("Plugged: latched AC D20 still calls set_desired_ac_params") {
        auto ctx = fx.make_ctx();
        api::AcIsoD20SessionParams d20{};
        d20.charging_current_a = 20.0f;
        d20.three_phases = false;
        ctx->configured_session = api::SessionConfigParams{d20};
        Plugged s{*ctx};
        auto result = s.feed(Event{BeginSessionEvt{}});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::SlacMatching);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=20)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "set_three_phases(three_phases=false)"));
    }

    SECTION("Plugged: latched DcIsoD20{mcs=set} -> SlacMatching") {
        auto ctx = fx.make_ctx();
        api::DcIsoD20SessionParams params{};
        params.mcs_enabled = true;
        ctx->configured_session = api::SessionConfigParams{params};
        Plugged s{*ctx};
        auto result = s.feed(Event{BeginSessionEvt{}});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::SlacMatching);
        CHECK(ctx->vars.charge_mode() == api::ChargeMode::DcIsoD20);
        CHECK(ctx->vars.mcs_enabled());
        // No reject ack should have been recorded.
        CHECK_FALSE(topic_recorded(fx.sink, ack_topic));
    }

    SECTION("Plugged: reconfigure mid-session leaves live session; next plug uses new config") {
        auto ctx = fx.make_ctx();
        ctx->configured_session = api::SessionConfigParams{api::AcIecSessionParams{20.0f, false, std::nullopt}};
        Plugged s{*ctx};
        s.feed(Event{BeginSessionEvt{}});
        REQUIRE(ctx->vars.session.has_value());
        CHECK(ctx->vars.charge_mode() == api::ChargeMode::AcIec);

        // Reconfigure latches a new spec but must not touch the live session.
        ctx->configure_session(api::SessionConfigParams{api::DcIso2SessionParams{}});
        CHECK(ctx->vars.charge_mode() == api::ChargeMode::AcIec);
        REQUIRE(ctx->configured_session.has_value());
        CHECK(api::mode_of(*ctx->configured_session) == api::ChargeMode::DcIso2);

        // unplug -> plug consumes the new config.
        Unplugged u{*ctx};
        u.enter();
        CHECK_FALSE(ctx->vars.session.has_value());
        Plugged s2{*ctx};
        auto result = s2.feed(Event{BeginSessionEvt{}});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::SlacMatching);
        CHECK(ctx->vars.charge_mode() == api::ChargeMode::DcIso2);
    }

    // ---- Plugged + ChargingCurve --------------------------------------

    SECTION("Plugged: latched ChargingCurve stashes pending_curve, does not splice") {
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};
        api::AcIecSessionParams params{};
        params.charging_current_a = 16.0f;
        params.three_phases = true;
        api::ChargingCurve curve;
        curve.loop = false;
        api::CurvePoint p0;
        p0.t_offset_ms = 2000;
        p0.current_a = 12.0f;
        p0.three_phases = true;
        curve.points.push_back(p0);
        api::CurvePoint p1;
        p1.t_offset_ms = 5000;
        p1.current_a = 8.0f;
        p1.three_phases = false;
        curve.points.push_back(p1);
        params.curve = curve;
        ctx->configured_session = api::SessionConfigParams{params};
        auto result = s.feed(Event{BeginSessionEvt{}});
        CHECK(result.new_state == nullptr);
        CHECK(ctx->vars.charge_mode() == api::ChargeMode::AcIec);
        // Curve is stashed for Charging::enter; scenario dispatcher stays idle.
        REQUIRE(ctx->vars.pending_curve().has_value());
        CHECK(ctx->vars.pending_curve()->points.size() == 2);
        CHECK_FALSE(ctx->scenario.active());
        CHECK_FALSE(topic_recorded(fx.sink, ack_topic));
    }

    SECTION("Plugged: latched DcIso2 offset-0 curve does not splice or enqueue") {
        // Offset-0 curve point would land in the runtime queue while still in
        // SlacMatching/V2GNegotiating/BcbToggling, all of which reject
        // SetChargingCurrent. Stashing on ctx.vars defers the splice to
        // Charging::enter where the FSM can consume it.
        auto ctx = fx.make_ctx();
        Plugged s{*ctx};
        api::DcIso2SessionParams params{};
        api::ChargingCurve curve;
        curve.loop = false;
        api::CurvePoint p0;
        p0.t_offset_ms = 0;
        p0.current_a = 10.0f;
        p0.three_phases = false;
        curve.points.push_back(p0);
        api::CurvePoint p1;
        p1.t_offset_ms = 1000;
        p1.current_a = 20.0f;
        p1.three_phases = false;
        curve.points.push_back(p1);
        params.curve = curve;
        ctx->configured_session = api::SessionConfigParams{params};
        auto result = s.feed(Event{BeginSessionEvt{}});
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::SlacMatching);
        // No SetChargingCurrent event went onto the runtime queue.
        CHECK(fx.timer.enqueued_events.empty());
        // No Rejected command_ack for set_charging_current.
        CHECK_FALSE(topic_recorded(fx.sink, ack_topic));
        // Curve was stashed, not spliced.
        REQUIRE(ctx->vars.pending_curve().has_value());
        CHECK_FALSE(ctx->scenario.active());
    }

    SECTION("Charging::enter splices pending_curve and fires offset-0 inline") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        api::ChargingCurve curve;
        curve.loop = false;
        api::CurvePoint p0;
        p0.t_offset_ms = 0;
        p0.current_a = 10.0f;
        p0.three_phases = false;
        curve.points.push_back(p0);
        api::CurvePoint p1;
        p1.t_offset_ms = 1000;
        p1.current_a = 20.0f;
        p1.three_phases = false;
        curve.points.push_back(p1);
        ensure_session(*ctx).pending_curve = curve;

        Charging s{*ctx};
        s.enter();

        // pending_curve was consumed.
        CHECK_FALSE(ctx->vars.pending_curve().has_value());
        // Dispatcher is active with one step still pending (t=1000).
        CHECK(ctx->scenario.active());
        CHECK(ctx->scenario.step_count() == 2);
        // The offset-0 step fired inline via ctx.enqueue.
        REQUIRE(fx.timer.enqueued_events.size() == 1);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::SetChargingCurrent);
        const auto& sc = std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[0].payload);
        CHECK(sc.current_a == 10.0f);
        CHECK_FALSE(sc.three_phases);
    }

    SECTION("Charging::enter splices pending_curve with loop=true") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        api::ChargingCurve curve;
        curve.loop = true;
        api::CurvePoint p0;
        p0.t_offset_ms = 1000;
        p0.current_a = 12.0f;
        p0.three_phases = true;
        curve.points.push_back(p0);
        api::CurvePoint p1;
        p1.t_offset_ms = 3000;
        p1.current_a = 8.0f;
        p1.three_phases = true;
        curve.points.push_back(p1);
        ensure_session(*ctx).pending_curve = curve;

        Charging s{*ctx};
        s.enter();

        CHECK_FALSE(ctx->vars.pending_curve().has_value());
        REQUIRE(ctx->scenario.active());
        CHECK(ctx->scenario.step_count() == 2);
        // First non-immediate step (at=1000ms) is armed; nothing enqueued yet.
        CHECK(fx.timer.enqueued_events.empty());

        // Fire 1: enqueues step 0 (current=12), idx -> 1, no rewind (1 < 2),
        // next arm ~2000ms for step 1.
        ctx->scenario.on_timer_fire(*ctx);
        REQUIRE(fx.timer.enqueued_events.size() == 1);
        CHECK(std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[0].payload).current_a == 12.0f);

        // Fire 2: enqueues step 1 (current=8), idx -> 2 == loop_end -> rewind
        // to loop_start=0, rebase start_at_. arm_next's inline flush sees
        // step 0 (at=1000, elapsed~1000) with non-positive delay and enqueues
        // it inline (current=12). idx -> 1, no further rewind. step 1 then
        // has a positive delay (~2000ms) so arm_next arms and returns.
        ctx->scenario.on_timer_fire(*ctx);
        REQUIRE(fx.timer.enqueued_events.size() == 3);
        CHECK(std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[1].payload).current_a == 8.0f);
        CHECK(std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[2].payload).current_a == 12.0f);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::SetChargingCurrent);
        CHECK(kind_of(fx.timer.enqueued_events[1]) == EventKind::SetChargingCurrent);
        CHECK(kind_of(fx.timer.enqueued_events[2]) == EventKind::SetChargingCurrent);

        // Dispatcher remains active and re-armed with a strictly positive
        // delay (a zero arm would disarm the underlying timerfd).
        CHECK(ctx->scenario.active());
        REQUIRE_FALSE(fx.timer.scenario_timer_arms.empty());
        CHECK(fx.timer.scenario_timer_arms.back() > std::chrono::milliseconds(0));
    }

    SECTION("Charging::enter without pending_curve is a no-op for the dispatcher") {
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        Charging s{*ctx};
        s.enter();
        CHECK_FALSE(ctx->vars.pending_curve().has_value());
        CHECK_FALSE(ctx->scenario.active());
        CHECK(fx.timer.enqueued_events.empty());
    }

    SECTION("Unplugged::enter clears pending_curve so it does not leak across sessions") {
        auto ctx = fx.make_ctx();
        api::ChargingCurve curve;
        curve.loop = false;
        api::CurvePoint p0;
        p0.t_offset_ms = 0;
        p0.current_a = 10.0f;
        curve.points.push_back(p0);
        ensure_session(*ctx).pending_curve = curve;

        Unplugged s{*ctx};
        s.enter();
        CHECK_FALSE(ctx->vars.pending_curve().has_value());
        CHECK_FALSE(ctx->vars.session.has_value());
    }

    SECTION("Faulted::enter clears pending_curve") {
        auto ctx = fx.make_ctx();
        api::ChargingCurve curve;
        api::CurvePoint p0;
        p0.t_offset_ms = 0;
        p0.current_a = 10.0f;
        curve.points.push_back(p0);
        ensure_session(*ctx).pending_curve = curve;
        ctx->vars.last_fault = api::FaultReport{api::FaultType::Internal, std::string{"x"}, std::nullopt};

        Faulted s{*ctx};
        s.enter();
        CHECK_FALSE(ctx->vars.pending_curve().has_value());
        CHECK_FALSE(ctx->vars.session.has_value());
    }

    SECTION("transition_to_disabled clears pending_curve") {
        auto ctx = fx.make_ctx();
        api::ChargingCurve curve;
        api::CurvePoint p0;
        p0.t_offset_ms = 0;
        p0.current_a = 10.0f;
        curve.points.push_back(p0);
        ensure_session(*ctx).pending_curve = curve;

        auto result = transition_to_disabled(*ctx);
        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::Disabled);
        CHECK_FALSE(ctx->vars.pending_curve().has_value());
        CHECK_FALSE(ctx->vars.session.has_value());
    }

    SECTION("Unplugged::enter clears payment so it does not leak across sessions") {
        auto ctx = fx.make_ctx();
        ensure_session(*ctx).payment = api::PaymentOption::Contract;
        Unplugged s{*ctx};
        s.enter();
        CHECK_FALSE(ctx->vars.payment().has_value());
    }

    SECTION("Faulted::enter clears payment") {
        auto ctx = fx.make_ctx();
        ensure_session(*ctx).payment = api::PaymentOption::Contract;
        ctx->vars.last_fault = api::FaultReport{api::FaultType::Internal, std::string{"x"}, std::nullopt};
        Faulted s{*ctx};
        s.enter();
        CHECK_FALSE(ctx->vars.payment().has_value());
    }

    SECTION("transition_to_disabled clears payment") {
        auto ctx = fx.make_ctx();
        ensure_session(*ctx).payment = api::PaymentOption::Contract;
        auto result = transition_to_disabled(*ctx);
        REQUIRE(result.new_state);
        CHECK_FALSE(ctx->vars.payment().has_value());
    }

    SECTION("session teardown drops all session state in one shot") {
        // Populate every session-scoped field, then exercise each of the
        // three end paths. After any of them ctx.vars.session is nullopt and
        // every former field reads as "no session" — no field can be
        // forgotten because there is only one thing to reset.
        auto populate = [](FsmContext& c) {
            api::ChargingCurve curve;
            api::CurvePoint p0;
            p0.t_offset_ms = 0;
            p0.current_a = 10.0f;
            curve.points.push_back(p0);
            auto& sess = ensure_session(c);
            sess.mode = api::ChargeMode::DcIsoD20;
            sess.payment = api::PaymentOption::Contract;
            sess.bpt = api::BptParams{};
            sess.mcs_enabled = true;
            sess.pending_curve = curve;
        };
        auto assert_gone = [](FsmContext& c) {
            CHECK_FALSE(c.vars.session.has_value());
            CHECK_FALSE(c.vars.charge_mode().has_value());
            CHECK_FALSE(c.vars.payment().has_value());
            CHECK_FALSE(c.vars.bpt().has_value());
            CHECK_FALSE(c.vars.mcs_enabled());
            CHECK_FALSE(c.vars.pending_curve().has_value());
        };

        SECTION("Unplugged::enter") {
            auto ctx = fx.make_ctx();
            populate(*ctx);
            Unplugged s{*ctx};
            s.enter();
            assert_gone(*ctx);
        }
        SECTION("Faulted::enter") {
            auto ctx = fx.make_ctx();
            populate(*ctx);
            ctx->vars.last_fault = api::FaultReport{api::FaultType::Internal, std::string{"x"}, std::nullopt};
            Faulted s{*ctx};
            s.enter();
            assert_gone(*ctx);
        }
        SECTION("transition_to_disabled") {
            auto ctx = fx.make_ctx();
            populate(*ctx);
            auto result = transition_to_disabled(*ctx);
            REQUIRE(result.new_state);
            assert_gone(*ctx);
        }
    }

    SECTION("full session lifecycle per mode populates then clears session") {
        struct Case {
            api::SessionConfigParams params;
            api::ChargeMode mode;
        };
        std::vector<Case> cases{
            {api::SessionConfigParams{api::AcIecSessionParams{20.0f, false, std::nullopt}}, api::ChargeMode::AcIec},
            {api::SessionConfigParams{api::AcIso2SessionParams{}}, api::ChargeMode::AcIso2},
            {api::SessionConfigParams{api::AcIsoD20SessionParams{}}, api::ChargeMode::AcIsoD20},
            {api::SessionConfigParams{api::DcIso2SessionParams{}}, api::ChargeMode::DcIso2},
            {api::SessionConfigParams{api::DcIsoD20SessionParams{}}, api::ChargeMode::DcIsoD20},
        };
        for (const auto& tc : cases) {
            auto ctx = fx.make_ctx();
            ctx->configured_session = tc.params;
            Plugged s{*ctx};
            s.feed(Event{BeginSessionEvt{}});
            REQUIRE(ctx->vars.session.has_value());
            CHECK(ctx->vars.charge_mode() == tc.mode);

            Unplugged u{*ctx};
            u.enter();
            CHECK_FALSE(ctx->vars.session.has_value());
            CHECK_FALSE(ctx->vars.charge_mode().has_value());
        }
    }

    SECTION("Unplugged::enter clears was_full so on_battery_full re-arms next session") {
        auto ctx = fx.make_ctx();
        ctx->vars.was_full = true;
        Unplugged s{*ctx};
        s.enter();
        CHECK_FALSE(ctx->vars.was_full);
    }

    SECTION("Faulted::enter clears was_full") {
        auto ctx = fx.make_ctx();
        ctx->vars.was_full = true;
        ctx->vars.last_fault = api::FaultReport{api::FaultType::Internal, std::string{"x"}, std::nullopt};
        Faulted s{*ctx};
        s.enter();
        CHECK_FALSE(ctx->vars.was_full);
    }

    SECTION("Faulted::enter without last_fault still clears was_full") {
        auto ctx = fx.make_ctx();
        ctx->vars.was_full = true;
        ctx->vars.last_fault.reset();
        Faulted s{*ctx};
        s.enter();
        CHECK_FALSE(ctx->vars.was_full);
    }

    SECTION("transition_to_disabled clears was_full") {
        auto ctx = fx.make_ctx();
        ctx->vars.was_full = true;
        auto result = transition_to_disabled(*ctx);
        REQUIRE(result.new_state);
        CHECK_FALSE(ctx->vars.was_full);
    }

    SECTION("AcIec end-to-end: Plugged -> Charging via PWM fires offset-0 step inline") {
        // Drives the full handoff: BeginSession (post-plug self-advance)
        // resolves the latched config and stashes the curve while Plugged
        // stays put; the subsequent BspMeasurement (PWM in the (7, 97)
        // window) transitions to Charging, whose enter() splices and fires
        // the offset-0 step. No SetChargingCurrent ack is rejected anywhere
        // along the path.
        auto ctx = fx.make_ctx();
        Plugged plugged{*ctx};

        api::AcIecSessionParams params{};
        api::ChargingCurve curve;
        curve.loop = false;
        api::CurvePoint p0;
        p0.t_offset_ms = 0;
        p0.current_a = 14.0f;
        p0.three_phases = true;
        curve.points.push_back(p0);
        api::CurvePoint p1;
        p1.t_offset_ms = 2000;
        p1.current_a = 10.0f;
        p1.three_phases = true;
        curve.points.push_back(p1);
        params.curve = curve;
        ctx->configured_session = api::SessionConfigParams{params};

        auto start_result = plugged.feed(Event{BeginSessionEvt{}});
        // AcIec stays in Plugged until PWM crosses into (7, 97); curve is stashed.
        CHECK(start_result.new_state == nullptr);
        REQUIRE(ctx->vars.pending_curve().has_value());
        CHECK_FALSE(ctx->scenario.active());
        CHECK(fx.timer.enqueued_events.empty());
        // Nothing rejected from the BeginSession step either.
        CHECK_FALSE(topic_recorded(fx.sink, ack_topic));

        // PWM enters the charging window -> Plugged -> Charging.
        Event bsp_ev{EventKind::BspMeasurement};
        BspMeasurementPayload m{};
        m.cp_pwm_duty_cycle = 50;
        bsp_ev.payload = m;
        auto bsp_result = plugged.feed(bsp_ev);
        REQUIRE(bsp_result.new_state);
        CHECK(bsp_result.new_state->get_id() == api::FsmState::Charging);

        // Charging::enter splices and fires the offset-0 point inline.
        bsp_result.new_state->enter();
        CHECK_FALSE(ctx->vars.pending_curve().has_value());
        REQUIRE(ctx->scenario.active());
        REQUIRE(fx.timer.enqueued_events.size() == 1);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::SetChargingCurrent);
        const auto& sc = std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[0].payload);
        CHECK(sc.current_a == 14.0f);
        // No Rejected set_charging_current ack landed on the bus.
        CHECK_FALSE(topic_recorded(fx.sink, ack_topic));
    }

    SECTION("DcIso2 end-to-end: Plugged -> SlacMatching -> V2GNegotiating -> Charging") {
        // Drives Plugged -> SlacMatching -> V2GNegotiating -> Charging via
        // the canonical DC ISO 2 event sequence. The offset-0 curve point
        // must reach the runtime queue exactly once, via Charging::enter, and
        // none of the intermediate states must publish a Rejected
        // set_charging_current ack.
        auto ctx = fx.make_ctx();
        Plugged plugged{*ctx};

        api::DcIso2SessionParams params{};
        api::ChargingCurve curve;
        curve.loop = false;
        api::CurvePoint p0;
        p0.t_offset_ms = 0;
        p0.current_a = 32.0f;
        p0.three_phases = false;
        curve.points.push_back(p0);
        api::CurvePoint p1;
        p1.t_offset_ms = 5000;
        p1.current_a = 16.0f;
        p1.three_phases = false;
        curve.points.push_back(p1);
        params.curve = curve;
        ctx->configured_session = api::SessionConfigParams{params};

        auto start_result = plugged.feed(Event{BeginSessionEvt{}});
        REQUIRE(start_result.new_state);
        CHECK(start_result.new_state->get_id() == api::FsmState::SlacMatching);
        // Curve stashed, dispatcher idle, nothing enqueued yet.
        REQUIRE(ctx->vars.pending_curve().has_value());
        CHECK_FALSE(ctx->scenario.active());
        CHECK(fx.timer.enqueued_events.empty());

        start_result.new_state->enter();
        SlacMatching& slac = static_cast<SlacMatching&>(*start_result.new_state);
        Event slac_ev{EventKind::SlacState};
        slac_ev.payload = SlacStatePayload{"MATCHED"};
        auto slac_result = slac.feed(slac_ev);
        REQUIRE(slac_result.new_state);
        CHECK(slac_result.new_state->get_id() == api::FsmState::V2GNegotiating);
        CHECK(fx.timer.enqueued_events.empty());

        slac_result.new_state->enter();
        V2GNegotiating& v2g = static_cast<V2GNegotiating&>(*slac_result.new_state);
        // DC ISO: ev_power_ready (CableCheck/PreCharge gate) asserts CP=C and
        // holds; it does NOT enter Charging. dc_power_on (PreCharge complete)
        // is the charge-loop entry milestone.
        auto pr_result = v2g.feed(Event{EventKind::IsoPowerReady});
        CHECK(pr_result.new_state == nullptr);
        auto v2g_result = v2g.feed(Event{EventKind::IsoDcPowerOn});
        REQUIRE(v2g_result.new_state);
        CHECK(v2g_result.new_state->get_id() == api::FsmState::Charging);
        CHECK(fx.timer.enqueued_events.empty());

        v2g_result.new_state->enter();
        // Charging::enter has spliced and fired the offset-0 step inline.
        CHECK_FALSE(ctx->vars.pending_curve().has_value());
        REQUIRE(ctx->scenario.active());
        REQUIRE(fx.timer.enqueued_events.size() == 1);
        CHECK(kind_of(fx.timer.enqueued_events[0]) == EventKind::SetChargingCurrent);
        const auto& sc = std::get<api::SetChargingCurrentParams>(fx.timer.enqueued_events[0].payload);
        CHECK(sc.current_a == 32.0f);
        // No state along the chain emitted a Rejected set_charging_current.
        CHECK_FALSE(topic_recorded(fx.sink, ack_topic));
    }
}

// ---- exception isolation in event dispatch -------------------------------
//
// The event variant is the single source of truth: an event's discriminant
// is its active alternative, so a producer cannot construct a tag/payload
// mismatch and `std::get<T>(ev.payload)` in a state handler can no longer
// throw `std::bad_variant_access`. `feed_with_fault_isolation` still guards
// the loop thread against any exception escaping `feed` (e.g. a throwing
// peer action callback): it logs the failure and forces the FSM into
// Faulted(Internal).

namespace {

// Builds an FsmContext whose bsp.set_cp peer action throws when driven to
// Cp state B. Unplugged::enter() drives state A (so initial seeding is fine);
// Plugged::enter() drives state B, so a Unplugged -> Plugged transition
// surfaces the exception out of fsm->feed without any contrived variant state.
std::unique_ptr<FsmContext> make_ctx_with_throwing_set_cp(TestFixture& fx) {
    PeerActions actions = make_peer_actions(fx.mocks);
    actions.bsp.set_cp = [&fx](::types::ev_board_support::EvCpState s) {
        if (s == ::types::ev_board_support::EvCpState::B) {
            throw std::runtime_error("simulated peer action failure");
        }
        fx.mocks.bsp.call_set_cp_state(s);
    };
    return std::make_unique<FsmContext>(
        std::move(actions), [&fx](const std::string& topic, const std::string& payload) { fx.sink(topic, payload); },
        [&fx](std::chrono::milliseconds ms) { fx.timer.arm(ms); }, [&fx]() { fx.timer.cancel(); },
        [&fx](int ms) { fx.timer.arm_tick(ms); }, [&fx]() { fx.timer.disarm_tick(); },
        [&fx](Event ev) { fx.timer.record_enqueue(std::move(ev)); },
        [&fx](std::chrono::milliseconds ms) { fx.timer.arm_scenario(ms); }, fx.cfg, fx.topics);
}

} // namespace

TEST_CASE("EvSimRuntime feed_with_fault_isolation", "[evsim][fault_isolation]") {
    TestFixture fx;

    SECTION("raw fsm->feed propagates an exception from a peer action") {
        auto ctx = make_ctx_with_throwing_set_cp(fx);
        auto fsm = std::make_unique<fsm::v2::FSM<StateBase>>(std::make_unique<Unplugged>(*ctx));

        CHECK_THROWS_AS(fsm->feed(Event{PlugCmd{}}), std::runtime_error);
    }

    SECTION("feed_with_fault_isolation catches the exception and reseeds to Faulted(Internal)") {
        auto ctx = make_ctx_with_throwing_set_cp(fx);
        auto fsm = std::make_unique<fsm::v2::FSM<StateBase>>(std::make_unique<Unplugged>(*ctx));
        CHECK(fsm->get_current_state_id() == api::FsmState::Unplugged);

        // No exception escapes the wrapper.
        CHECK_NOTHROW(feed_with_fault_isolation(fsm, *ctx, Event{PlugCmd{}}));

        // FSM has been replaced with a Faulted state.
        REQUIRE(fsm);
        CHECK(fsm->get_current_state_id() == api::FsmState::Faulted);

        // Fault metadata reflects Internal cause.
        REQUIRE(ctx->vars.last_fault.has_value());
        CHECK(ctx->vars.last_fault->type == api::FaultType::Internal);
        REQUIRE(ctx->vars.last_fault->message.has_value());
        CHECK_FALSE(ctx->vars.last_fault->message->empty());

        // Faulted::enter has published the fault + state externally. The
        // Unplugged ctor also published once on the state topic, so take the
        // most recent state record.
        auto state_topic = fx.topics.everest_to_extern("state");
        auto last_state_it = std::find_if(fx.sink.records.rbegin(), fx.sink.records.rend(),
                                          [&](const auto& kv) { return kv.first == state_topic; });
        REQUIRE(last_state_it != fx.sink.records.rend());
        auto decoded_state = api::deserialize<api::FsmState>(last_state_it->second);
        CHECK(decoded_state == api::FsmState::Faulted);
        // The fault topic carries the Internal FaultReport.
        auto fault_topic = fx.topics.everest_to_extern("fault");
        REQUIRE(topic_recorded(fx.sink, fault_topic));
        auto decoded_fault = api::deserialize<api::FaultReport>(payload_for(fx.sink, fault_topic));
        CHECK(decoded_fault.type == api::FaultType::Internal);
    }

    SECTION("feed_with_fault_isolation is a pass-through for well-formed events") {
        auto ctx = fx.make_ctx();
        auto fsm = std::make_unique<fsm::v2::FSM<StateBase>>(std::make_unique<Unplugged>(*ctx));

        CHECK_NOTHROW(feed_with_fault_isolation(fsm, *ctx, Event{PlugCmd{}}));
        CHECK(fsm->get_current_state_id() == api::FsmState::Plugged);
        CHECK_FALSE(ctx->vars.last_fault.has_value());
    }
}
