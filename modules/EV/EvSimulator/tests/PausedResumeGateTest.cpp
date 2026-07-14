// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/FsmContext.hpp"
#include "../main/StateBase.hpp"
#include "../main/states/BcbToggling.hpp"
#include "../main/states/Paused.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <everest_api_types/ev_simulator/codec.hpp>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_simulator;

// An explicit BcbToggle (the smoke-test resume path) must honor the same
// iso_session_active gate as ResumeSession. Waking the SECC with BCB edges while
// the paused V2G session is still tearing down lets the previous session's
// lagging SessionStop clobber the freshly re-established link, so an explicit
// BcbToggle that arrives while the session is live must DEFER until
// IsoV2GFinished, just like an implicit ResumeSession.
TEST_CASE("EvSimulator BcbToggle defers while V2G session live", "[evsim][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.iso_session_active = true;

    Paused p{*ctx};
    Event ev{EventKind::BcbToggle};
    ev.payload = api::BcbToggleParams{3};
    auto result = p.feed(ev);

    // Deferred: stay in Paused, mark the resume pending, do not seed bcb_remaining
    // yet (it is released into bcb_remaining only when the session finishes).
    CHECK(result.new_state == nullptr);
    CHECK(ctx->vars.resume_pending);
    CHECK(ctx->vars.bcb_remaining == 0);
}

// A deferred explicit BcbToggle remembers its round-trip count (bcb_pending =
// 2 * count) and releases it into bcb_remaining when IsoV2GFinished arrives,
// so the user-requested count survives the defer.
TEST_CASE("EvSimulator deferred BcbToggle releases its count on IsoV2GFinished", "[evsim][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.iso_session_active = true;

    Paused p{*ctx};
    Event ev{EventKind::BcbToggle};
    ev.payload = api::BcbToggleParams{4};
    auto deferred = p.feed(ev);
    REQUIRE(deferred.new_state == nullptr);

    // The paused session finishes (apply_passthrough_vars clears the flag
    // pre-feed; emulated here), then IsoV2GFinished releases the deferred toggle.
    ctx->vars.iso_session_active = false;
    auto resumed = p.feed(Event{EventKind::IsoV2GFinished});
    REQUIRE(resumed.new_state);
    CHECK(resumed.new_state->get_id() == api::FsmState::BcbToggling);
    CHECK(ctx->vars.bcb_remaining == 8); // 2 * 4
    CHECK_FALSE(ctx->vars.resume_pending);
}

// A deferred implicit ResumeSession releases the default 6 BCB edges into
// bcb_remaining when IsoV2GFinished arrives.
TEST_CASE("EvSimulator deferred ResumeSession releases 6 edges on IsoV2GFinished", "[evsim][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.iso_session_active = true;

    Paused p{*ctx};
    auto deferred = p.feed(Event{EventKind::ResumeSession});
    REQUIRE(deferred.new_state == nullptr);
    CHECK(ctx->vars.resume_pending);

    ctx->vars.iso_session_active = false;
    auto resumed = p.feed(Event{EventKind::IsoV2GFinished});
    REQUIRE(resumed.new_state);
    CHECK(resumed.new_state->get_id() == api::FsmState::BcbToggling);
    CHECK(ctx->vars.bcb_remaining == 6);
    CHECK_FALSE(ctx->vars.resume_pending);
}

// If Josev never publishes v2g_session_finished (abnormal teardown), a deferred
// resume cannot wait forever for IsoV2GFinished: the short fallback timer armed
// on defer fires a StateDeadline, and with resume_pending set the EV does a
// best-effort resume into BcbToggling rather than giving up into Stopping.
TEST_CASE("EvSimulator deferred resume falls back to BcbToggling on StateDeadline", "[evsim][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.iso_session_active = true;

    Paused p{*ctx};
    auto deferred = p.feed(Event{EventKind::ResumeSession});
    REQUIRE(deferred.new_state == nullptr);
    CHECK(ctx->vars.resume_pending);

    // No IsoV2GFinished arrives; the fallback deadline fires while the resume is
    // still pending. Best-effort: resume into BcbToggling, not Stopping.
    auto fallback = p.feed(Event{EventKind::StateDeadline});
    REQUIRE(fallback.new_state);
    CHECK(fallback.new_state->get_id() == api::FsmState::BcbToggling);
    CHECK(ctx->vars.bcb_remaining == 6);
    CHECK_FALSE(ctx->vars.resume_pending);
}

// Regression guard: an idle pause (no resume requested) that hits its deadline
// still ends the session by transitioning to Stopping. The fallback only
// re-routes a deadline when a resume is actually pending.
TEST_CASE("EvSimulator idle Paused StateDeadline still stops the session", "[evsim][group2]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);

    Paused p{*ctx};
    REQUIRE_FALSE(ctx->vars.resume_pending);
    auto result = p.feed(Event{EventKind::StateDeadline});
    REQUIRE(result.new_state);
    CHECK(result.new_state->get_id() == api::FsmState::Stopping);
}
