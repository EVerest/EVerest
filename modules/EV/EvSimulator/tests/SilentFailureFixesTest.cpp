// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Coverage for the silent-failure tightening fixes:
//  - kvs_load distinguishes missing key, empty payload, and corrupt JSON.
//  - kvs_load_raw returns std::optional<std::string> so empty != missing.
//  - ScenarioDispatcher::start emits a CommandAck Rejected on unknown enum.
//  - Enum to_json throws std::out_of_range on out-of-range values.
//  - parse_on_battery_full throws on unknown values; FsmContext ctor surfaces
//    the throw so a typo aborts module init.
//  - SlacMatching/V2GNegotiating route to Faulted when their peer call returns
//    false rather than waiting for the deadline timer.
//  - PersistedState::from_json throws when "plugged_in" is absent instead of
//    silently defaulting to false.

#include "../main/FsmContext.hpp"
#include "../main/ScenarioDispatcher.hpp"
#include "../main/SocIntegrator.hpp"
#include "../main/states/SlacMatching.hpp"
#include "../main/states/V2GNegotiating.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <everest_api_types/ev_simulator/codec.hpp>
#include <everest_api_types/ev_simulator/json_codec.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <variant>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_simulator;

TEST_CASE("kvs_load distinguishes missing / empty / corrupt", "[evsim][kvs]") {
    TestFixture fx;
    fx.cfg.keep_cross_boot_plugin_state = true;

    SECTION("empty payload resets persisted to defaults") {
        fx.mocks.next_kvs_load_value = std::string{""};
        auto ctx = fx.make_ctx();
        // Pre-seed to confirm the empty branch resets rather than retains.
        ctx->mark_plugged_in(true);
        ctx->remember_session_config(api::SessionConfigParams{api::DcIso2SessionParams{}});

        ctx->kvs_load();

        CHECK(ctx->persisted_state().plugged_in == false);
        CHECK_FALSE(ctx->persisted_state().configured_session.has_value());
    }

    SECTION("corrupt JSON does not throw and resets persisted") {
        fx.mocks.next_kvs_load_value = std::string{"{not valid json"};
        auto ctx = fx.make_ctx();
        // Pre-seed to confirm the corrupt branch resets rather than partial-fills.
        ctx->mark_plugged_in(true);
        ctx->remember_session_config(api::SessionConfigParams{api::DcIso2SessionParams{}});

        REQUIRE_NOTHROW(ctx->kvs_load());

        CHECK(ctx->persisted_state().plugged_in == false);
        CHECK_FALSE(ctx->persisted_state().configured_session.has_value());
    }

    SECTION("JSON type mismatch on field resets persisted") {
        // plugged_in must be bool; supplying a string triggers
        // nlohmann::json::type_error inside the get<PersistedState>() call.
        nlohmann::json bogus = {{"plugged_in", "yes"}};
        fx.mocks.next_kvs_load_value = bogus.dump();
        auto ctx = fx.make_ctx();
        ctx->mark_plugged_in(true);
        ctx->remember_session_config(api::SessionConfigParams{api::DcIso2SessionParams{}});

        REQUIRE_NOTHROW(ctx->kvs_load());

        CHECK(ctx->persisted_state().plugged_in == false);
        CHECK_FALSE(ctx->persisted_state().configured_session.has_value());
    }
}

TEST_CASE("kvs_load_raw seam returns optional<string>", "[evsim][kvs][optional]") {
    TestFixture fx;
    fx.cfg.keep_cross_boot_plugin_state = true;

    SECTION("nullopt models missing key") {
        fx.mocks.next_kvs_load_value = std::nullopt;
        auto ctx = fx.make_ctx();

        auto out = ctx->peer_actions.kvs.load_raw("any");

        CHECK_FALSE(out.has_value());
    }

    SECTION("optional empty string models persisted empty payload") {
        fx.mocks.next_kvs_load_value = std::string{""};
        auto ctx = fx.make_ctx();

        auto out = ctx->peer_actions.kvs.load_raw("any");

        REQUIRE(out.has_value());
        CHECK(out->empty());
    }

    SECTION("optional non-empty string round-trips") {
        fx.mocks.next_kvs_load_value = std::string{"hello"};
        auto ctx = fx.make_ctx();

        auto out = ctx->peer_actions.kvs.load_raw("any");

        REQUIRE(out.has_value());
        CHECK(*out == "hello");
    }
}

TEST_CASE("ScenarioDispatcher unknown enum value publishes Rejected ack", "[evsim][scenario][unknown]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    const auto ack_topic = fx.topics.everest_to_extern("command_ack");

    // Cast an integer outside the defined range to force the default branch.
    auto bogus = static_cast<api::ScenarioName>(9999);

    REQUIRE_NOTHROW(ctx->scenario.start(bogus, std::nullopt, *ctx));

    // Dispatcher must not become active for an unknown scenario.
    CHECK_FALSE(ctx->scenario.active());

    auto it = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                           [&](const auto& kv) { return kv.first == ack_topic; });
    REQUIRE(it != fx.sink.records.end());
    auto ack = api::deserialize<api::CommandAck>(it->second);
    CHECK(ack.command == "run_scenario");
    CHECK(ack.status == api::CommandAckStatus::Rejected);
    REQUIRE(ack.reason.has_value());
    CHECK(*ack.reason == "unknown scenario name");
}

TEST_CASE("Enum to_json throws on out-of-range value", "[evsim][codec][to_json]") {
    SECTION("ScenarioName out-of-range throws std::out_of_range") {
        auto bogus = static_cast<api::ScenarioName>(9999);
        nlohmann::json j;
        REQUIRE_THROWS_AS(api::to_json(j, bogus), std::out_of_range);
    }

    SECTION("FsmState out-of-range throws std::out_of_range") {
        auto bogus = static_cast<api::FsmState>(9999);
        nlohmann::json j;
        REQUIRE_THROWS_AS(api::to_json(j, bogus), std::out_of_range);
    }

    SECTION("CommandAckStatus out-of-range throws std::out_of_range") {
        auto bogus = static_cast<api::CommandAckStatus>(9999);
        nlohmann::json j;
        REQUIRE_THROWS_AS(api::to_json(j, bogus), std::out_of_range);
    }
}

TEST_CASE("parse_on_battery_full rejects unknown values", "[evsim][config]") {
    SECTION("known values map to their enum") {
        CHECK(parse_on_battery_full("clamp") == OnBatteryFull::Clamp);
        CHECK(parse_on_battery_full("idle_at_full") == OnBatteryFull::IdleAtFull);
        CHECK(parse_on_battery_full("stop_session") == OnBatteryFull::StopSession);
        CHECK(parse_on_battery_full("pause_if_iso") == OnBatteryFull::PauseIfIso);
    }

    SECTION("typo throws std::invalid_argument") {
        REQUIRE_THROWS_AS(parse_on_battery_full("stopsesion"), std::invalid_argument);
    }

    SECTION("empty string throws std::invalid_argument") {
        REQUIRE_THROWS_AS(parse_on_battery_full(""), std::invalid_argument);
    }
}

TEST_CASE("FsmContext ctor validates cfg.on_battery_full", "[evsim][config]") {
    SECTION("recognized value constructs successfully and caches the policy") {
        TestFixture fx;
        fx.cfg.on_battery_full = "stop_session";
        auto ctx = fx.make_ctx();
        CHECK(ctx->on_battery_full_policy == OnBatteryFull::StopSession);
    }

    SECTION("unknown value aborts ctor by propagating std::invalid_argument") {
        TestFixture fx;
        fx.cfg.on_battery_full = "stopsesion"; // typo
        REQUIRE_THROWS_AS(fx.make_ctx(), std::invalid_argument);
    }
}

TEST_CASE("SlacMatching::enter routes to Faulted when peer is unwired", "[evsim][slac]") {
    using namespace module;
    using EK = module::EventKind;

    SECTION("missing slac peer carries message on the InjectFault, not last_fault") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        // Model a missing ev_slac peer.
        ctx->peer_actions.slac = {};

        SlacMatching s{*ctx};
        s.enter();

        // enter() no longer pre-seeds last_fault: the descriptive message
        // travels on the InjectFault payload so an intervening transition
        // cannot leave it stale for a later Faulted entry.
        CHECK_FALSE(ctx->vars.last_fault.has_value());

        auto it = std::find_if(fx.timer.enqueued_events.begin(), fx.timer.enqueued_events.end(),
                               [](const module::Event& e) { return kind_of(e) == EK::InjectFault; });
        REQUIRE(it != fx.timer.enqueued_events.end());
        auto p = std::get<api::InjectFaultParams>(it->payload);
        CHECK(p.type == api::FaultType::Internal);
        REQUIRE(p.message.has_value());
        CHECK(*p.message == "no ev_slac peer");
        // No deadline timer is armed in the failure path so the state cannot
        // sit around for 30s and then fault with the wrong reason.
        CHECK(fx.timer.state_timer_arms.empty());
    }

    SECTION("wired peer returning false carries a distinct message") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        // Override the slac trigger_matching seam to return false (peer still
        // wired/present, but the matching attempt fails).
        ctx->peer_actions.slac.trigger_matching = []() -> bool { return false; };

        SlacMatching s{*ctx};
        s.enter();

        CHECK_FALSE(ctx->vars.last_fault.has_value());
        auto it = std::find_if(fx.timer.enqueued_events.begin(), fx.timer.enqueued_events.end(),
                               [](const module::Event& e) { return kind_of(e) == EK::InjectFault; });
        REQUIRE(it != fx.timer.enqueued_events.end());
        auto p = std::get<api::InjectFaultParams>(it->payload);
        CHECK(p.type == api::FaultType::Internal);
        REQUIRE(p.message.has_value());
        CHECK(*p.message == "ev_slac trigger_matching rejected");
        CHECK(fx.timer.state_timer_arms.empty());
    }
}

TEST_CASE("V2GNegotiating::enter routes to Faulted when iso_start_charging returns false", "[evsim][v2g]") {
    using namespace module;
    using EK = module::EventKind;

    SECTION("missing iso peer in an ISO mode carries message on the InjectFault") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        // Model a missing iso peer so iso_start_charging short-circuits to false.
        ctx->peer_actions.iso = {};

        V2GNegotiating s{*ctx};
        s.enter();

        CHECK_FALSE(ctx->vars.last_fault.has_value());

        auto it = std::find_if(fx.timer.enqueued_events.begin(), fx.timer.enqueued_events.end(),
                               [](const module::Event& e) { return kind_of(e) == EK::InjectFault; });
        REQUIRE(it != fx.timer.enqueued_events.end());
        auto p = std::get<api::InjectFaultParams>(it->payload);
        CHECK(p.type == api::FaultType::Internal);
        REQUIRE(p.message.has_value());
        CHECK(*p.message == "iso_start_charging rejected");
        CHECK(fx.timer.state_timer_arms.empty());
    }

    SECTION("AcIec charge mode (iso_start_charging returns false for AcIec) enqueues InjectFault") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec); // iso_start_charging returns false unconditionally for AcIec

        V2GNegotiating s{*ctx};
        s.enter();

        CHECK_FALSE(ctx->vars.last_fault.has_value());
        auto it = std::find_if(fx.timer.enqueued_events.begin(), fx.timer.enqueued_events.end(),
                               [](const module::Event& e) { return kind_of(e) == EK::InjectFault; });
        REQUIRE(it != fx.timer.enqueued_events.end());
        auto p = std::get<api::InjectFaultParams>(it->payload);
        CHECK(p.type == api::FaultType::Internal);
        REQUIRE(p.message.has_value());
        CHECK(*p.message == "iso_start_charging rejected");
    }
}

TEST_CASE("transition_to_fault preserves a pre-seeded message of the same type", "[evsim][fault]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    // Pre-seed last_fault as the fault-on-entry pattern does.
    ctx->vars.last_fault =
        api::FaultReport{api::FaultType::Internal, std::string{"rich precondition message"}, std::nullopt};

    api::InjectFaultParams params{api::FaultType::Internal, std::nullopt};
    auto result = transition_to_fault(*ctx, params);

    REQUIRE(ctx->vars.last_fault.has_value());
    CHECK(ctx->vars.last_fault->type == api::FaultType::Internal);
    REQUIRE(ctx->vars.last_fault->message.has_value());
    CHECK(*ctx->vars.last_fault->message == "rich precondition message");
    REQUIRE(result.new_state);
}

TEST_CASE("transition_to_fault replaces last_fault when the injected type differs", "[evsim][fault]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->vars.last_fault = api::FaultReport{api::FaultType::Internal, std::string{"stale message"}, std::nullopt};

    api::InjectFaultParams params{api::FaultType::DiodeFail, std::nullopt};
    auto result = transition_to_fault(*ctx, params);

    REQUIRE(ctx->vars.last_fault.has_value());
    CHECK(ctx->vars.last_fault->type == api::FaultType::DiodeFail);
    CHECK_FALSE(ctx->vars.last_fault->message.has_value());
    REQUIRE(result.new_state);
}

TEST_CASE("PersistedState from_json throws when plugged_in is missing", "[evsim][kvs]") {
    SECTION("payload without plugged_in throws and kvs_load resets to defaults") {
        TestFixture fx;
        fx.cfg.keep_cross_boot_plugin_state = true;
        // Valid JSON object, but missing the required "plugged_in" key.
        nlohmann::json j = nlohmann::json::object();
        fx.mocks.next_kvs_load_value = j.dump();

        auto ctx = fx.make_ctx();
        ctx->mark_plugged_in(true); // pre-seed to confirm reset
        REQUIRE_NOTHROW(ctx->kvs_load());
        CHECK(ctx->persisted_state().plugged_in == false);
    }

    SECTION("direct from_json on a missing-key payload throws") {
        nlohmann::json j = nlohmann::json::object();
        PersistedState s;
        REQUIRE_THROWS(from_json(j, s));
    }
}

// A peer's `present` flag is the single signal for "this peer is wired".
// build_peer_actions / make_peer_actions set the flag in the same block that
// wires the functions, so `present == true` implies every function of that
// peer is callable. A partially wired peer (some functions set, some null)
// cannot be expressed.
TEST_CASE("PeerActions presence implies all of a peer's functions are callable", "[evsim][peers]") {
    TestFixture fx;

    SECTION("present iso peer: every iso function is invocable, not null") {
        auto ctx = fx.make_ctx();
        REQUIRE(ctx->peer_actions.iso.present);
        // Each iso function is set; calling them must not throw a bad_function_call.
        CHECK(static_cast<bool>(ctx->peer_actions.iso.start_charging));
        CHECK(static_cast<bool>(ctx->peer_actions.iso.stop_charging));
        CHECK(static_cast<bool>(ctx->peer_actions.iso.pause_charging));
        CHECK(static_cast<bool>(ctx->peer_actions.iso.update_soc));
        CHECK(static_cast<bool>(ctx->peer_actions.iso.enable_sae_j2847_v2g_v2h));
        CHECK(static_cast<bool>(ctx->peer_actions.iso.set_bpt_dc_params));
        set_mode(*ctx, api::ChargeMode::DcIso2);
        // iso_start_charging reaches the wired peer (returns the mock's result).
        CHECK(ctx->iso_start_charging(api::ChargeMode::DcIso2, std::nullopt, 0, 0));
        REQUIRE_NOTHROW(ctx->iso_stop_charging());
        REQUIRE_NOTHROW(ctx->iso_pause_charging());
        REQUIRE_NOTHROW(ctx->iso_update_soc(42.0f));
    }

    SECTION("absent iso peer: iso shortcuts no-op / return false, same as before") {
        auto ctx = fx.make_ctx();
        ctx->peer_actions.iso = {};
        REQUIRE_FALSE(ctx->peer_actions.iso.present);
        set_mode(*ctx, api::ChargeMode::DcIso2);
        fx.mocks.iso.clear();
        CHECK_FALSE(ctx->iso_start_charging(api::ChargeMode::DcIso2, std::nullopt, 0, 0));
        REQUIRE_NOTHROW(ctx->iso_stop_charging());
        REQUIRE_NOTHROW(ctx->iso_pause_charging());
        REQUIRE_NOTHROW(ctx->iso_update_soc(42.0f));
        CHECK(fx.mocks.iso.records.empty());
    }

    SECTION("absent slac peer: trigger returns false") {
        auto ctx = fx.make_ctx();
        ctx->peer_actions.slac = {};
        REQUIRE_FALSE(ctx->peer_actions.slac.present);
        CHECK_FALSE(ctx->slac_trigger_matching());
    }
}

// Out-of-band error raise/clear is routed onto the loop thread: the MQTT
// command router only enqueues a RaiseErrorCmd / ClearErrorCmd, and the loop
// thread flushes it and drives FsmContext::raise_error / clear_error. These
// pin the loop-thread behavior, including the no-op clear that publishes a
// Rejected ack instead of silently doing nothing.
TEST_CASE("raise_error / clear_error route through the loop thread", "[evsim][errors]") {
    TestFixture fx;

    SECTION("raise_error drives the error sink with the parsed args") {
        auto ctx = fx.make_ctx();
        RaiseErrorCmd cmd;
        cmd.type = "generic/VendorError";
        cmd.sub_type = "sub";
        cmd.message = "boom";
        cmd.severity = Everest::error::Severity::High;
        ctx->raise_error(cmd);
        REQUIRE(fx.mocks.error_raises.size() == 1);
        CHECK(fx.mocks.error_raises[0] == "raise(type=generic/VendorError,sub=sub,msg=boom)");
        CHECK(fx.mocks.error_clears.empty());
    }

    SECTION("clear_error of an active error reaches the sink") {
        auto ctx = fx.make_ctx();
        fx.mocks.active_errors.emplace_back("generic/VendorError", "sub");
        ClearErrorCmd cmd;
        cmd.type = "generic/VendorError";
        cmd.sub_type = "sub";
        ctx->clear_error(cmd);
        REQUIRE(fx.mocks.error_clears.size() == 1);
        CHECK(fx.mocks.error_clears[0] == "clear(type=generic/VendorError,sub=sub)");
    }

    SECTION("clear_error of an inactive error is a no-op with a Rejected ack") {
        auto ctx = fx.make_ctx();
        ClearErrorCmd cmd;
        cmd.type = "generic/VendorError";
        cmd.sub_type = "sub";
        ctx->clear_error(cmd);
        CHECK(fx.mocks.error_clears.empty());
        auto ack_topic = fx.topics.everest_to_extern("command_ack");
        auto it = std::find_if(fx.sink.records.begin(), fx.sink.records.end(),
                               [&](const auto& kv) { return kv.first == ack_topic; });
        REQUIRE(it != fx.sink.records.end());
        auto ack = api::deserialize<api::CommandAck>(it->second);
        CHECK(ack.command == "clear_error");
        CHECK(ack.status == api::CommandAckStatus::Rejected);
        REQUIRE(ack.reason.has_value());
        CHECK(*ack.reason == "no such error active");
    }

    SECTION("RaiseError / ClearError events round-trip through the queue") {
        // The command router enqueues these; the kind discriminant survives so
        // the loop thread can recover the parsed payload via std::get_if.
        Event raise_ev{RaiseErrorCmd{"generic/VendorError", "s", "m", Everest::error::Severity::High}};
        Event clear_ev{ClearErrorCmd{"generic/VendorError", std::nullopt}};
        CHECK(kind_of(raise_ev) == EventKind::RaiseError);
        CHECK(kind_of(clear_ev) == EventKind::ClearError);
        REQUIRE(std::get_if<RaiseErrorCmd>(&raise_ev.payload) != nullptr);
        REQUIRE(std::get_if<ClearErrorCmd>(&clear_ev.payload) != nullptr);
        CHECK(std::get_if<RaiseErrorCmd>(&raise_ev.payload)->message == "m");
    }

    SECTION("error sink absent: raise/clear are silent no-ops") {
        auto ctx = fx.make_ctx();
        ctx->peer_actions.error = {};
        REQUIRE_FALSE(ctx->peer_actions.error.present);
        RaiseErrorCmd rc;
        rc.type = "generic/VendorError";
        REQUIRE_NOTHROW(ctx->raise_error(rc));
        ClearErrorCmd cc;
        cc.type = "generic/VendorError";
        REQUIRE_NOTHROW(ctx->clear_error(cc));
        CHECK(fx.mocks.error_raises.empty());
        CHECK(fx.mocks.error_clears.empty());
    }
}
