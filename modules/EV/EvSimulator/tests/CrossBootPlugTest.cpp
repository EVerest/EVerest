// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/Events.hpp"
#include "../main/FsmContext.hpp"
#include "../main/states/Plugged.hpp"
#include "../main/states/Unplugged.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_simulator;

namespace {

constexpr auto plugged_dc_blob =
    R"({"plugged_in":true,"configured_session":{"mode":"DcIso2","params":{"payment":"ExternalPayment"}}})";

constexpr auto idle_dc_blob =
    R"({"plugged_in":true,"hold_session":true,"configured_session":{"mode":"DcIso2","params":{"payment":"ExternalPayment"}}})";

int count_kind(const TimerSink& timer, EventKind kind) {
    int count = 0;
    for (const auto& ev : timer.enqueued_events) {
        count += kind_of(ev) == kind ? 1 : 0;
    }
    return count;
}

int plug_count(const TimerSink& timer) {
    return count_kind(timer, EventKind::Plug);
}

bool replug_armed(const TimerSink& timer) {
    return timer.state_timer_arms.size() == 1 && timer.state_timer_arms[0] == Unplugged::replug_dwell;
}

// Enters Unplugged as the first state after the load, then plugs on the dwell deadline.
std::unique_ptr<StateBase> restore(FsmContext& ctx) {
    Unplugged unplugged{ctx};
    unplugged.enter();
    auto result = unplugged.feed(Event{EventKind::StateDeadline});
    REQUIRE(result.new_state);
    unplugged.leave();
    result.new_state->enter();
    return std::move(result.new_state);
}

// The blob of the most recent kvs store, as a fresh boot would load it.
std::string last_stored_blob(const ActionMocks& mocks) {
    const std::string marker = ",json=";
    for (auto it = mocks.kvs.records.rbegin(); it != mocks.kvs.records.rend(); ++it) {
        const auto at = it->find(marker);
        if (it->rfind("store(", 0) == 0 && at != std::string::npos) {
            return it->substr(at + marker.size(), it->size() - at - marker.size() - 1);
        }
    }
    FAIL("no kvs store recorded");
    return {};
}

// Boots a fresh context from the last stored blob and restores it.
int begin_sessions_after_reboot(TestFixture& fx) {
    fx.mocks.next_kvs_load_value = last_stored_blob(fx.mocks);
    fx.timer.clear();
    auto ctx = fx.make_ctx();
    ctx->kvs_load();
    auto plugged = restore(*ctx);
    REQUIRE(plugged->get_id() == api::FsmState::Plugged);
    return count_kind(fx.timer, EventKind::BeginSession);
}

} // namespace

TEST_CASE("a plug persisted before a reboot is restored", "[evsim][kvs]") {
    TestFixture fx;
    fx.cfg.keep_cross_boot_plugin_state = true;

    SECTION("first Unplugged entry re-plugs after the dwell and keeps the persisted plug") {
        fx.mocks.next_kvs_load_value = std::string{plugged_dc_blob};
        auto ctx = fx.make_ctx();
        ctx->kvs_load();

        Unplugged{*ctx}.enter();

        CHECK(replug_armed(fx.timer));
        CHECK(plug_count(fx.timer) == 0);
        CHECK(ctx->persisted_state().plugged_in == true);
        CHECK_FALSE(contains_substr(fx.mocks.kvs.records, R"("plugged_in":false)"));
        REQUIRE(ctx->configured_session.has_value());
        CHECK(api::mode_of(*ctx->configured_session) == api::ChargeMode::DcIso2);
    }

    SECTION("the restore is one-shot") {
        fx.mocks.next_kvs_load_value = std::string{plugged_dc_blob};
        auto ctx = fx.make_ctx();
        ctx->kvs_load();
        Unplugged{*ctx}.enter();
        fx.timer.clear();

        Unplugged{*ctx}.enter();

        CHECK(fx.timer.state_timer_arms.empty());
        CHECK(ctx->persisted_state().plugged_in == false);
    }

    SECTION("an unplugged blob does not plug") {
        fx.mocks.next_kvs_load_value = std::string{R"({"plugged_in":false})"};
        auto ctx = fx.make_ctx();
        ctx->kvs_load();

        Unplugged{*ctx}.enter();

        CHECK(fx.timer.state_timer_arms.empty());
    }

    SECTION("first boot does not plug") {
        auto ctx = fx.make_ctx();
        ctx->kvs_load();

        Unplugged{*ctx}.enter();

        CHECK(fx.timer.state_timer_arms.empty());
    }

    SECTION("a restored session begins with the persisted mode") {
        fx.mocks.next_kvs_load_value = std::string{plugged_dc_blob};
        auto ctx = fx.make_ctx();
        ctx->kvs_load();

        auto plugged = restore(*ctx);
        REQUIRE(plugged->get_id() == api::FsmState::Plugged);
        REQUIRE(count_kind(fx.timer, EventKind::BeginSession) == 1);
        const auto begin = std::find_if(fx.timer.enqueued_events.begin(), fx.timer.enqueued_events.end(),
                                        [](const Event& ev) { return kind_of(ev) == EventKind::BeginSession; });
        auto result = plugged->feed(*begin);

        REQUIRE(result.new_state);
        CHECK(result.new_state->get_id() == api::FsmState::SlacMatching);
        CHECK(ctx->vars.charge_mode() == api::ChargeMode::DcIso2);
    }

    SECTION("an EV plugged with no session restores idle") {
        fx.mocks.next_kvs_load_value = std::string{idle_dc_blob};
        auto ctx = fx.make_ctx();
        ctx->kvs_load();

        auto plugged = restore(*ctx);

        CHECK(plugged->get_id() == api::FsmState::Plugged);
        CHECK(count_kind(fx.timer, EventKind::BeginSession) == 0);
        CHECK(ctx->vars.hold_session);
        CHECK(ctx->persisted_state().hold_session);
        CHECK_FALSE(ctx->vars.session.has_value());
    }

    SECTION("the idle state persists with the plug") {
        auto ctx = fx.make_ctx();
        ctx->vars.hold_session = true;

        Plugged{*ctx}.enter();

        CHECK(contains_substr(fx.mocks.kvs.records, R"("hold_session":true)"));
    }

    SECTION("a persisted spec that no longer validates is dropped with its plug") {
        api::DcIso2SessionParams params;
        api::ChargingCurve curve;
        curve.loop = true;
        curve.points = {api::CurvePoint{}};
        params.curve = curve;
        PersistedState blob;
        blob.plugged_in = true;
        blob.configured_session = api::SessionConfigParams{params};
        fx.mocks.next_kvs_load_value = nlohmann::json(blob).dump();
        auto ctx = fx.make_ctx();
        ctx->kvs_load();

        CHECK_FALSE(ctx->configured_session.has_value());
        CHECK_FALSE(ctx->persisted_state().configured_session.has_value());
        CHECK_FALSE(ctx->persisted_state().plugged_in);
        CHECK_FALSE(ctx->take_restore_plug());

        // Still Disabled, with no Unplugged entry to overwrite the plug: a save
        // from configure_session must not write the rejected plug back.
        ctx->configure_session(api::SessionConfigParams{api::AcIecSessionParams{}});
        fx.mocks.next_kvs_load_value = last_stored_blob(fx.mocks);
        fx.timer.clear();
        auto reboot = fx.make_ctx();
        reboot->kvs_load();

        Unplugged{*reboot}.enter();

        CHECK(fx.timer.state_timer_arms.empty());
        CHECK_FALSE(reboot->persisted_state().plugged_in);
    }

    SECTION("a persisted spec the codec rejects is dropped with its plug") {
        fx.mocks.next_kvs_load_value = std::string{
            R"({"plugged_in":true,"configured_session":{"mode":"DcIso2","params":{"curve":{"loop":false,"points":[)"
            R"({"t_offset_ms":1000,"current_a":10.0,"three_phases":false},)"
            R"({"t_offset_ms":500,"current_a":10.0,"three_phases":false}]}}}})"};
        auto ctx = fx.make_ctx();

        CHECK_NOTHROW(ctx->kvs_load());
        CHECK_FALSE(ctx->configured_session.has_value());

        Unplugged{*ctx}.enter();

        CHECK(fx.timer.state_timer_arms.empty());
        CHECK(ctx->persisted_state().plugged_in == false);
    }
}

TEST_CASE("a restart during the replug dwell restores the session asked for", "[evsim][kvs][replug]") {
    TestFixture fx;
    fx.cfg.keep_cross_boot_plugin_state = true;

    SECTION("a plug while held") {
        auto ctx = fx.make_ctx();
        ctx->configure_session(api::SessionConfigParams{api::DcIso2SessionParams{}});
        ctx->vars.hold_session = true;
        Plugged held{*ctx};
        held.enter();
        auto result = held.feed(Event{EventKind::Plug});
        REQUIRE(result.new_state);
        REQUIRE(result.new_state->get_id() == api::FsmState::Unplugged);
        held.leave();
        result.new_state->enter();

        CHECK(begin_sessions_after_reboot(fx) == 1);
    }

    SECTION("a plug during an idle restore") {
        fx.mocks.next_kvs_load_value = std::string{idle_dc_blob};
        auto ctx = fx.make_ctx();
        ctx->kvs_load();
        Unplugged unplugged{*ctx};
        unplugged.enter();
        REQUIRE(replug_armed(fx.timer));
        REQUIRE(unplugged.feed(Event{EventKind::Plug}).new_state == nullptr);

        CHECK(begin_sessions_after_reboot(fx) == 1);
    }
}

TEST_CASE("a persisted plug is ignored without keep_cross_boot_plugin_state", "[evsim][kvs]") {
    TestFixture fx;
    fx.cfg.keep_cross_boot_plugin_state = false;
    fx.mocks.next_kvs_load_value = std::string{plugged_dc_blob};
    auto ctx = fx.make_ctx();
    ctx->kvs_load();

    Unplugged{*ctx}.enter();

    CHECK(fx.timer.state_timer_arms.empty());
}
