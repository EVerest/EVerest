// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/states/Disabled.hpp"
#include "../main/states/Unplugged.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <everest/util/fsm/fsm.hpp>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_controller;
using Pilot = ::types::board_support_common::Event;

TEST_CASE("old plugged and held blobs wait for the current pilot", "[evctl][kvs][pilot-only]") {
    const auto held = GENERATE(false, true);
    const auto present = GENERATE(false, true);
    TestFixture fx;
    fx.cfg.keep_cross_boot_plugin_state = true;
    fx.mocks.next_kvs_load_value =
        nlohmann::json{{"plugged_in", true},
                       {"hold_session", held},
                       {"configured_session", {{"mode", "DcIso2"}, {"params", {{"payment", "ExternalPayment"}}}}}}
            .dump();
    auto ctx = fx.make_ctx();
    ctx->kvs_load();
    fsm::v2::FSM<StateBase> machine{std::make_unique<Disabled>(*ctx)};
    machine.feed(pilot_input(present ? Pilot::A : Pilot::Disconnected));
    machine.feed(Event{EnableCmd{}});
    CHECK(fx.timer.state_timer_arms.empty());
    CHECK_FALSE(ctx->vars.hold_session);
    if (!present) {
        CHECK(fx.timer.enqueued_events.empty());
        machine.feed(Event{StateDeadlineEvt{}});
        CHECK(machine.get_current_state_id() == api::FsmState::Unplugged);
        CHECK_FALSE(ctx->vars.session);
        machine.feed(pilot_input(Pilot::A));
    }
    while (!fx.timer.enqueued_events.empty()) {
        auto event = fx.timer.enqueued_events.front();
        fx.timer.enqueued_events.erase(fx.timer.enqueued_events.begin());
        machine.feed(event);
    }
    CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
    CHECK(ctx->vars.charge_mode() == api::ChargeMode::DcIso2);
    CHECK_FALSE(ctx->vars.hold_session);
    machine.feed(Event{BspMeasurementPayload{5.0f, {}, {}}});
    CHECK(machine.get_current_state_id() == api::FsmState::SlacMatching);
}

TEST_CASE("persisted configured sessions are validated before pilot start", "[evctl][kvs]") {
    TestFixture fx;
    fx.cfg.keep_cross_boot_plugin_state = true;
    SECTION("a looping zero-duration curve is invalid") {
        api::DcIso2SessionParams params;
        params.curve = api::ChargingCurve{{api::CurvePoint{}}, true};
        PersistedState blob;
        blob.configured_session = params;
        fx.mocks.next_kvs_load_value = nlohmann::json(blob).dump();
    }
    SECTION("a curve the codec rejects is invalid") {
        fx.mocks.next_kvs_load_value = std::string{
            R"({"plugged_in":true,"configured_session":{"mode":"DcIso2","params":{"curve":{"loop":false,"points":[)"
            R"({"t_offset_ms":1000,"current_a":10.0,"three_phases":false},)"
            R"({"t_offset_ms":500,"current_a":10.0,"three_phases":false}]}}}})"};
    }
    auto ctx = fx.make_ctx();
    CHECK_NOTHROW(ctx->kvs_load());
    CHECK_FALSE(ctx->configured_session);
    CHECK_FALSE(ctx->persisted_state().configured_session);
    Unplugged{*ctx}.enter();
    CHECK(fx.timer.enqueued_events.empty());
    CHECK(fx.timer.state_timer_arms.empty());
}

TEST_CASE("keep_cross_boot_plugin_state without a kvs connection is refused", "[evctl][kvs][config]") {
    Conf cfg{};
    cfg.keep_cross_boot_plugin_state = true;

    const auto error = cross_boot_config_error(cfg, false);

    REQUIRE(error.has_value());
    CHECK(error->find("keep_cross_boot_plugin_state") != std::string::npos);
    CHECK(error->find("kvs") != std::string::npos);
}

TEST_CASE("keep_cross_boot_plugin_state is accepted with a kvs connection", "[evctl][kvs][config]") {
    Conf cfg{};
    cfg.keep_cross_boot_plugin_state = true;

    CHECK_FALSE(cross_boot_config_error(cfg, true).has_value());
}

TEST_CASE("no kvs connection is needed without keep_cross_boot_plugin_state", "[evctl][kvs][config]") {
    Conf cfg{};
    cfg.keep_cross_boot_plugin_state = false;

    CHECK_FALSE(cross_boot_config_error(cfg, false).has_value());
}

TEST_CASE("configured session persistence is optional and ignores old plug fields", "[evctl][kvs]") {
    const auto enabled = GENERATE(false, true);
    TestFixture fx;
    fx.cfg.keep_cross_boot_plugin_state = enabled;
    fx.mocks.next_kvs_load_value =
        R"({"plugged_in":true,"hold_session":true,"configured_session":{"mode":"AcIec","params":{"charging_current_a":12}}})";
    auto ctx = fx.make_ctx();
    ctx->kvs_load();
    CHECK(ctx->configured_session.has_value() == enabled);
    ctx->kvs_save();
    if (enabled) {
        auto persisted = nlohmann::json(ctx->persisted_state());
        CHECK(persisted.contains("configured_session"));
        CHECK_FALSE(persisted.contains("plugged_in"));
        CHECK_FALSE(persisted.contains("hold_session"));
    } else {
        CHECK(fx.mocks.kvs.records.empty());
    }
}

TEST_CASE("persisted session blobs accept missing legacy flags but require an object", "[evctl][kvs]") {
    PersistedState state;
    CHECK_NOTHROW(from_json(nlohmann::json::object(), state));
    CHECK_FALSE(state.configured_session);
    CHECK_THROWS(from_json(nlohmann::json(true), state));
    CHECK_THROWS(from_json(nlohmann::json::array(), state));
}
