// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "helper.hpp"

#include <catch2/generators/catch_generators.hpp>
#include <iso15118/d20/state/dc_cable_check.hpp>
#include <iso15118/message/dc_cable_check.hpp>

TEST_CASE("ISO15118-20 cable check completion", "[dc-preparation-stop]") {
    std::optional<d20::PauseContext> pause_ctx;
    std::vector<session::feedback::Signal> signals;
    session::feedback::Callbacks callbacks{};
    callbacks.signal = [&](auto signal) { signals.push_back(signal); };
    FsmStateHelper helper(session::SessionConfig(create_default_evse_setup()), pause_ctx, callbacks);
    auto& ctx = helper.get_context();
    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_CableCheck>()};
    message_20::DC_CableCheckRequest req{};
    req.header.session_id = ctx.session.get_id();

    const bool already_started = GENERATE(false, true);
    // Repeated pending requests must not restart the check.
    for (int i = 0; already_started and i < 2; ++i) {
        helper.handle_request(req);
        REQUIRE_FALSE(fsm.feed(d20::Event::V2GTP_MESSAGE).transitioned());
        const auto res = ctx.get_response<message_20::DC_CableCheckResponse>();
        REQUIRE(res.has_value());
        CHECK(res->response_code == dt::ResponseCode::OK);
        CHECK(res->processing == dt::Processing::Ongoing);
        CHECK_FALSE(ctx.session_stopped);
        CHECK(fsm.get_current_state_id() == d20::StateID::DC_CableCheck);
    }
    if (already_started) {
        REQUIRE(signals == std::vector{session::feedback::Signal::START_CABLE_CHECK});
    }

    const bool success = GENERATE(false, true);
    helper.set_active_control_event(d20::CableCheckFinished{success});
    REQUIRE_FALSE(fsm.feed(d20::Event::CONTROL_MESSAGE).transitioned());
    helper.handle_request(req);
    const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);
    const auto res = ctx.get_response<message_20::DC_CableCheckResponse>();
    REQUIRE(res.has_value());
    CHECK(res->processing == dt::Processing::Finished);
    CHECK(res->response_code == (success ? dt::ResponseCode::OK : dt::ResponseCode::FAILED_SequenceError));
    CHECK(ctx.session_stopped == not success);
    CHECK(result.transitioned() == success);
    CHECK(fsm.get_current_state_id() == (success ? d20::StateID::DC_PreCharge : d20::StateID::DC_CableCheck));
    CHECK(signals.size() == (already_started or success ? 1 : 0));
}
