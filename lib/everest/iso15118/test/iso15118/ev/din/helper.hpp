// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <everest/util/async/monitor.hpp>
#include <everest/util/fsm/fsm.hpp>

#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/states.hpp>
#include <iso15118/ev/session_params.hpp>
#include <iso15118/message_din/type.hpp>
#include <iso15118/message_din/variant.hpp>

using namespace iso15118;

inline constexpr message_din::datatypes::SessionId SESSION_ID{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02};
inline constexpr message_din::datatypes::SessionId WRONG_SESSION_ID{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00};

inline message_din::Header header(const message_din::datatypes::SessionId& session_id = SESSION_ID) {
    return message_din::Header{session_id};
}

class DinStateHelper {
public:
    DinStateHelper(const ev::feedback::Callbacks& callbacks, ev::EvSessionParams params = {},
                   bool has_cp_state_feedback = false,
                   std::optional<message_din::datatypes::SessionId> resumed_session_id = std::nullopt) :
        ctx(callbacks, msg_exch, std::move(params), control_event, dc_params, has_cp_state_feedback,
            resumed_session_id) {
    }

    ev::din::Context& get_context();

    ev::din::MessageExchange& get_message_exchange() {
        return msg_exch;
    }

    template <typename ResponseType> void handle_response(const ResponseType& response) {
        // Mirrors the Session: the request slot is empty by the time a response arrives, so drain it
        // here for the next state's enter().
        while (msg_exch.has_request()) {
            msg_exch.take_request();
        }
        msg_exch.set_response(std::make_unique<message_din::Variant>(response));
    }

    // Seed the module -> FSM DcChargeParams channel before creating a state.
    void set_dc_params(const ev::DcChargeParams& params) {
        auto h = dc_params.handle();
        *h = params;
    }

    // Set the active control event the Context reads via get_control_event<T>().
    void set_control_event(const ev::din::ControlEvent& event) {
        control_event = event;
    }

    void clear_control_event() {
        control_event.reset();
    }

private:
    ev::din::MessageExchange msg_exch{};
    everest::lib::util::monitor<ev::DcChargeParams> dc_params{ev::DcChargeParams{}};
    std::optional<ev::din::ControlEvent> control_event{};

    ev::din::Context ctx;
};

// Pending requests, EXI round-trip-decoded the way the Session transmits them; proves a request
// actually serializes, not just that a retained copy matches.
class DecodedRequests {
public:
    // First pending request (FIFO order) decodable as Msg; nullopt if none match.
    template <typename Msg> std::optional<Msg> get() const {
        for (const auto& variant : variants) {
            if (const auto* msg = variant->get_if<Msg>()) {
                return *msg;
            }
        }
        return std::nullopt;
    }

    // Decoded request types in FIFO order.
    std::vector<message_din::Type> types() const {
        std::vector<message_din::Type> out;
        out.reserve(variants.size());
        for (const auto& variant : variants) {
            out.push_back(variant->get_type());
        }
        return out;
    }

    bool empty() const {
        return variants.empty();
    }

    void add(std::unique_ptr<message_din::Variant> variant) {
        variants.push_back(std::move(variant));
    }

private:
    std::vector<std::unique_ptr<message_din::Variant>> variants;
};

// Pop and decode every pending request from the exchange (destructive, FIFO).
DecodedRequests take_all_requests(ev::din::MessageExchange& msg_exch);

// A no-op context seed: the default for states that only need the primed session id.
inline const auto no_seed = [](DinStateHelper&) {};

// A primed FSM fixture: primes the session id to SESSION_ID (so response echoes line up), runs
// `seed(helper)` before the state is entered, then enters State. The entry request queued by
// State::enter() is already pending.
template <typename State> struct PrimedState {
    // Seed must be callable, which is what keeps this from competing with the overload below when a
    // test does name session params.
    template <typename Seed, typename... Args, std::enable_if_t<std::is_invocable_v<Seed&, DinStateHelper&>, int> = 0>
    PrimedState(const ev::feedback::Callbacks& callbacks, Seed seed, Args&&... args) :
        PrimedState(callbacks, ev::EvSessionParams{}, false, seed, std::forward<Args>(args)...) {
    }

    // Values the Context only takes at construction (session params, CP-state feedback).
    template <typename Seed, typename... Args>
    PrimedState(const ev::feedback::Callbacks& callbacks, ev::EvSessionParams params, bool has_cp_state_feedback,
                Seed seed, Args&&... args) :
        helper(callbacks, std::move(params), has_cp_state_feedback),
        ctx(helper.get_context()),
        fsm(seed_and_enter(seed, std::forward<Args>(args)...)) {
    }

    template <typename ResponseType> void handle_response(const ResponseType& response) {
        helper.handle_response(response);
    }

    DecodedRequests take_requests() {
        return take_all_requests(helper.get_message_exchange());
    }

    auto feed(ev::din::Event event) {
        return fsm.feed(event);
    }

    DinStateHelper helper;
    ev::din::Context& ctx;
    fsm::v2::FSM<ev::din::StateBase> fsm;

private:
    template <typename Seed, typename... Args> ev::din::BasePointerType seed_and_enter(Seed& seed, Args&&... args) {
        ctx.set_session_id(SESSION_ID);
        seed(helper);
        return ctx.create_state<State>(std::forward<Args>(args)...);
    }
};

// Feed `response` and assert the stop tail every negative path shares: no transition, still
// `expected_id`, session stop requested.
template <typename ResponseType>
void expect_stops_session(DinStateHelper& helper, fsm::v2::FSM<ev::din::StateBase>& fsm, const ResponseType& response,
                          ev::din::StateID expected_id) {
    helper.handle_response(response);
    const auto result = fsm.feed(ev::din::Event::V2GTP_MESSAGE);
    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == expected_id);
    REQUIRE(helper.get_context().is_session_stopped() == true);
}

// Same stop tail, driven through a PrimedState.
template <typename Primed, typename ResponseType>
void expect_stops_session(Primed& primed, const ResponseType& response, ev::din::StateID expected_id) {
    expect_stops_session(primed.helper, primed.fsm, response, expected_id);
}

// The three rejection checks shared by every response-consuming state: FAILED response code,
// wrong-variant response, and a non-echoed session id. `make_fsm(helper)` builds the entered FSM;
// `make_ok(header)` builds the state's otherwise-valid OK response.
template <typename MakeFsm, typename MakeOk, typename WrongVariant>
void check_rejection_paths(const ev::feedback::Callbacks& callbacks, ev::din::StateID expected_id, MakeFsm make_fsm,
                           MakeOk make_ok, const WrongVariant& wrong_variant) {
    const auto run = [&](const auto& response) {
        DinStateHelper helper{callbacks};
        auto fsm = make_fsm(helper);
        expect_stops_session(helper, fsm, response, expected_id);
    };

    SECTION("stops the session on a FAILED response code") {
        auto res = make_ok(header());
        res.response_code = message_din::datatypes::ResponseCode::FAILED;
        run(res);
    }
    SECTION("stops the session on a wrong-variant response") {
        run(wrong_variant);
    }
    SECTION("stops the session on a mismatched response session_id") {
        run(make_ok(header(WRONG_SESSION_ID)));
    }
}
