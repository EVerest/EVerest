// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// configure_session interceptor contract. ConfigureSession is intercepted on
// the loop thread (EvSimRuntime::on_wake) before the FSM feed and handled by
// FsmContext::configure_session: validate the spec, then either latch it into
// ctx.configured_session with an Accepted command_ack, or reject it with a
// Rejected command_ack and leave configured_session untouched. Validation is
// curve well-formedness (non-empty, strictly monotonic t_offset_ms) plus
// ISO/SLAC peer presence for ISO charge modes.

#include "../main/FsmContext.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <everest_api_types/ev_simulator/codec.hpp>

#include <optional>
#include <string>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_simulator;

namespace {

api::CommandAck last_ack(const PublisherSink& sink, const ev_API::Topics& topics) {
    auto topic = topics.everest_to_extern("command_ack");
    return api::deserialize<api::CommandAck>(payload_for(sink, topic));
}

} // namespace

TEST_CASE("configure_session interceptor latch/reject contract", "[evsim][configure_session]") {
    TestFixture fx;

    SECTION("valid AcIec config (no peers required) latches with Accepted ack") {
        auto ctx = fx.make_ctx();

        ctx->configure_session(api::SessionConfigParams{api::AcIecSessionParams{20.0f, false, std::nullopt}});

        REQUIRE(ctx->configured_session.has_value());
        CHECK(api::mode_of(*ctx->configured_session) == api::ChargeMode::AcIec);
        auto ack = last_ack(fx.sink, fx.topics);
        CHECK(ack.command == "configure_session");
        CHECK(ack.status == api::CommandAckStatus::Accepted);
    }

    SECTION("valid DcIso2 config with iso+slac peers present latches") {
        auto ctx = fx.make_ctx();

        ctx->configure_session(api::SessionConfigParams{api::DcIso2SessionParams{}});

        REQUIRE(ctx->configured_session.has_value());
        CHECK(api::mode_of(*ctx->configured_session) == api::ChargeMode::DcIso2);
        CHECK(last_ack(fx.sink, fx.topics).status == api::CommandAckStatus::Accepted);
    }

    SECTION("config persisted to KVS when keep_cross_boot_plugin_state") {
        fx.cfg.keep_cross_boot_plugin_state = true;
        auto ctx = fx.make_ctx();

        ctx->configure_session(api::SessionConfigParams{api::AcIecSessionParams{}});

        CHECK(contains_substr(fx.mocks.kvs.records, "store(key="));
    }

    SECTION("non-monotonic curve rejected; configured_session stays nullopt") {
        auto ctx = fx.make_ctx();
        api::AcIso2SessionParams p;
        p.curve = api::ChargingCurve{{{0, 10.0f, false, std::nullopt}, {0, 12.0f, false, std::nullopt}}, false};

        ctx->configure_session(api::SessionConfigParams{p});

        CHECK_FALSE(ctx->configured_session.has_value());
        auto ack = last_ack(fx.sink, fx.topics);
        CHECK(ack.command == "configure_session");
        CHECK(ack.status == api::CommandAckStatus::Rejected);
        REQUIRE(ack.reason.has_value());
        CHECK(ack.reason->find("monotonic") != std::string::npos);
    }

    SECTION("empty curve points rejected") {
        auto ctx = fx.make_ctx();
        api::AcIso2SessionParams p;
        p.curve = api::ChargingCurve{{}, false};

        ctx->configure_session(api::SessionConfigParams{p});

        CHECK_FALSE(ctx->configured_session.has_value());
        CHECK(last_ack(fx.sink, fx.topics).status == api::CommandAckStatus::Rejected);
    }

    SECTION("ISO mode with no slac peer rejected, not latched") {
        auto ctx = fx.make_ctx();
        ctx->peer_actions.slac.present = false;

        ctx->configure_session(api::SessionConfigParams{api::DcIsoD20SessionParams{}});

        CHECK_FALSE(ctx->configured_session.has_value());
        auto ack = last_ack(fx.sink, fx.topics);
        CHECK(ack.status == api::CommandAckStatus::Rejected);
        REQUIRE(ack.reason.has_value());
        CHECK(ack.reason->find("ev_slac") != std::string::npos);
    }

    SECTION("ISO mode with no iso peer rejected") {
        auto ctx = fx.make_ctx();
        ctx->peer_actions.iso.present = false;

        ctx->configure_session(api::SessionConfigParams{api::AcIso2SessionParams{}});

        CHECK_FALSE(ctx->configured_session.has_value());
        CHECK(last_ack(fx.sink, fx.topics).status == api::CommandAckStatus::Rejected);
    }

    SECTION("reconfigure replaces a prior latched config") {
        auto ctx = fx.make_ctx();
        ctx->configure_session(api::SessionConfigParams{api::AcIecSessionParams{}});
        REQUIRE(ctx->configured_session.has_value());

        ctx->configure_session(api::SessionConfigParams{api::DcIso2SessionParams{}});

        REQUIRE(ctx->configured_session.has_value());
        CHECK(api::mode_of(*ctx->configured_session) == api::ChargeMode::DcIso2);
    }

    SECTION("rejected reconfigure leaves the prior latched config intact") {
        auto ctx = fx.make_ctx();
        ctx->configure_session(api::SessionConfigParams{api::AcIecSessionParams{}});
        REQUIRE(ctx->configured_session.has_value());

        api::AcIso2SessionParams bad;
        bad.curve = api::ChargingCurve{{}, false};
        ctx->configure_session(api::SessionConfigParams{bad});

        REQUIRE(ctx->configured_session.has_value());
        CHECK(api::mode_of(*ctx->configured_session) == api::ChargeMode::AcIec);
    }
}
