// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
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

#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/states.hpp>
#include <iso15118/ev/params/dc.hpp>
#include <iso15118/ev/params/iso2_ac.hpp>
#include <iso15118/ev/session/latches.hpp>
#include <iso15118/message_din/power_delivery.hpp>
#include <iso15118/message_din/response_variant.hpp>
#include <iso15118/message_din/session_stop.hpp>
#include <iso15118/message_din/type.hpp>
#include <iso15118/message_din/variant.hpp>
#include <iso15118/message_din/welding_detection.hpp>

using namespace iso15118;

inline constexpr message_din::datatypes::SessionId SESSION_ID{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02};
inline constexpr message_din::datatypes::SessionId WRONG_SESSION_ID{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00};

inline message_din::Header header(const message_din::datatypes::SessionId& session_id = SESSION_ID) {
    return message_din::Header{session_id};
}

class DinStateHelper {
public:
    DinStateHelper(const ev::feedback::Callbacks& callbacks, ev::params::EvSessionParams params = {},
                   bool has_cp_state_feedback = false,
                   std::optional<message_din::datatypes::SessionId> resumed_session_id = std::nullopt,
                   ev::config::WeldingDetectionConfig welding_detection = {}) :
        ctx(callbacks, msg_exch, std::move(params), latches, dc_params, has_cp_state_feedback, resumed_session_id,
            welding_detection) {
    }

    ev::din::Context& get_context();

    ev::din::MessageExchange& get_message_exchange() {
        return msg_exch;
    }

    template <typename ResponseType> void handle_response(const ResponseType& response) {
        // Mirrors the Session: the request slot is empty by the time a response arrives.
        while (msg_exch.has_request()) {
            msg_exch.take_request();
        }
        msg_exch.set_response(std::make_unique<message_din::ResponseVariant>(response));
    }

    // Seed the module -> FSM DcChargeParams channel before creating a state.
    void set_dc_params(const ev::params::DcChargeParams& params) {
        auto h = dc_params.handle();
        *h = params;
    }

    // What Session::deliver_control_event does before it feeds the event.
    void latch(const ev::din::ControlEvent& event) {
        ev::session::latch(latches, event);
    }

private:
    ev::din::MessageExchange msg_exch{};
    everest::lib::util::monitor<ev::params::DcChargeParams> dc_params{ev::params::DcChargeParams{}};
    ev::session::Latches latches{};

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

// DC parameters a fixture starts from when the test names none of its own.
inline ev::params::DcChargeParams default_dc_params() {
    ev::params::DcChargeParams params;
    params.max_charge_power = 60000.0f;
    params.max_charge_current = 150.0f;
    params.max_voltage = 900.0f;
    params.energy_capacity = 80000.0f;
    params.target_voltage = 400.0f;
    params.target_current = 100.0f;
    params.present_soc = 50.0;
    return params;
}

// Feeds a fresh Pred `response` and returns the state its transition created, with the first request
// that transition sent pending.
template <typename Pred, typename Response, typename... Args>
ev::din::BasePointerType successor_of(DinStateHelper& helper, const Response& response, Args&&... args) {
    auto pred = helper.get_context().create_state<Pred>(std::forward<Args>(args)...);
    helper.handle_response(response);
    auto result = pred->feed(ev::din::Event::V2GTP_MESSAGE);
    REQUIRE(result.output == ev::din::Disposition::Transitioning);
    REQUIRE(result.new_state != nullptr);
    return std::move(result.new_state);
}

// Passed as a PrimedState's only state argument: the state is entered through the transition
// `enter(helper)` performs instead of being created bare.
template <typename Enter> struct EnteredBy {
    Enter enter;
};

template <typename Enter> EnteredBy<Enter> entered_by(Enter enter) {
    return {std::move(enter)};
}

template <typename T> struct IsEnteredBy : std::false_type {};
template <typename Enter> struct IsEnteredBy<EnteredBy<Enter>> : std::true_type {};

// A primed FSM fixture: primes the session id to SESSION_ID (so response echoes line up), runs
// `seed(helper)` before the state is entered, then enters State. Created bare, nothing is pending: a
// state's first request goes out on the transition into it. Entered through an EnteredBy, the request
// that transition sent is pending.
template <typename State> struct PrimedState {
    template <typename Seed, typename... Args>
    PrimedState(const ev::feedback::Callbacks& callbacks, ev::config::WeldingDetectionConfig welding, Seed seed,
                Args&&... args) :
        helper(callbacks, {}, false, std::nullopt, welding),
        ctx(helper.get_context()),
        fsm(seed_and_enter(seed, std::forward<Args>(args)...)) {
    }

    // Seed must be callable, which is what keeps this from competing with the overload below when a
    // test does name session params.
    template <typename Seed, typename... Args, std::enable_if_t<std::is_invocable_v<Seed&, DinStateHelper&>, int> = 0>
    PrimedState(const ev::feedback::Callbacks& callbacks, Seed seed, Args&&... args) :
        PrimedState(callbacks, ev::params::EvSessionParams{}, false, seed, std::forward<Args>(args)...) {
    }

    // Values the Context only takes at construction (session params, CP-state feedback).
    template <typename Seed, typename... Args>
    PrimedState(const ev::feedback::Callbacks& callbacks, ev::params::EvSessionParams params,
                bool has_cp_state_feedback, Seed seed, Args&&... args) :
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
        if constexpr (sizeof...(Args) == 1 and (IsEnteredBy<std::decay_t<Args>>::value and ...)) {
            auto state = (args.enter(helper), ...);
            REQUIRE(dynamic_cast<State*>(state.get()) != nullptr);
            return state;
        } else {
            return ctx.create_state<State>(std::forward<Args>(args)...);
        }
    }
};

// A predecessor state fed outside an FSM, so no successor's enter() runs: whatever is pending after its
// feed was sent on the transition itself. The session id is SESSION_ID and the DC parameters are the
// defaults; `seed(helper)` runs before the predecessor is created.
class DinPredecessor {
public:
    template <typename State, typename Seed, typename... Args>
    static DinPredecessor make(ev::params::EvSessionParams params, bool has_cp_state_feedback, Seed seed,
                               Args&&... args) {
        DinPredecessor pred(std::move(params), has_cp_state_feedback);
        seed(*pred.helper);
        pred.current = pred.ctx.create_state<State>(std::forward<Args>(args)...);
        return pred;
    }

    template <typename State, typename... Args> static DinPredecessor of(Args&&... args) {
        return make<State>(ev::params::EvSessionParams{}, false, no_seed, std::forward<Args>(args)...);
    }

    // Feeds `response`, requires a Transitioning result into `successor`, and returns what the
    // transition sent. The successor's enter() then runs and must send nothing more; it is the state
    // the next feed reaches.
    template <typename Response> DecodedRequests transitions_to(ev::din::StateID successor, const Response& response) {
        helper->handle_response(response);
        return transitions_on(successor, ev::din::Event::V2GTP_MESSAGE);
    }

    // As transitions_to, for an event that carries no response.
    DecodedRequests transitions_on(ev::din::StateID successor, ev::din::Event event) {
        auto result = current->feed(event);
        REQUIRE(result.output == ev::din::Disposition::Transitioning);
        REQUIRE(result.new_state != nullptr);
        REQUIRE(result.new_state->get_id() == successor);
        auto sent = take_all_requests(helper->get_message_exchange());
        REQUIRE_FALSE(sent.empty());
        current = std::move(result.new_state);
        current->enter();
        REQUIRE_FALSE(helper->get_message_exchange().has_request());
        return sent;
    }

    // Feeds `response`, requires a Waiting result into `successor`, which holds its first request:
    // nothing is pending before or after its enter(). It is the state the next feed reaches.
    template <typename Response> void waits_in(ev::din::StateID successor, const Response& response) {
        helper->handle_response(response);
        auto result = current->feed(ev::din::Event::V2GTP_MESSAGE);
        REQUIRE(result.output == ev::din::Disposition::Waiting);
        REQUIRE(result.new_state != nullptr);
        REQUIRE(result.new_state->get_id() == successor);
        REQUIRE_FALSE(helper->get_message_exchange().has_request());
        current = std::move(result.new_state);
        current->enter();
        REQUIRE_FALSE(helper->get_message_exchange().has_request());
    }

    // Delivers a control event to the current state.
    ev::din::Result control(const ev::din::ControlEvent& event) {
        helper->latch(event);
        return current->feed(ev::din::Event::CONTROL_MESSAGE);
    }

    std::unique_ptr<DinStateHelper> helper;
    ev::din::Context& ctx;

private:
    DinPredecessor(ev::params::EvSessionParams params, bool has_cp_state_feedback) :
        helper(std::make_unique<DinStateHelper>(no_callbacks, std::move(params), has_cp_state_feedback)),
        ctx(helper->get_context()) {
        ctx.set_session_id(SESSION_ID);
        helper->set_dc_params(default_dc_params());
    }

    inline static const ev::feedback::Callbacks no_callbacks{};
    ev::din::BasePointerType current;
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

// From a pending PowerDelivery(Stop): accept it, pass welding detection at a safe voltage, accept
// SessionStop, and report whether the session was left joinable. DIN carries no reason on the wire.
template <typename Primed> bool walk_stop_paused(Primed& primed) {
    namespace dt = message_din::datatypes;
    REQUIRE(primed.fsm.get_current_state_id() == ev::din::StateID::PowerDelivery);
    message_din::PowerDeliveryResponse power_delivery{};
    power_delivery.header = header();
    power_delivery.response_code = dt::ResponseCode::OK;
    primed.handle_response(power_delivery);
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    message_din::WeldingDetectionResponse welding{};
    welding.header = header();
    welding.response_code = dt::ResponseCode::OK;
    welding.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    welding.evse_present_voltage = 0.0;
    primed.handle_response(welding);
    primed.feed(ev::din::Event::V2GTP_MESSAGE);
    REQUIRE(primed.fsm.get_current_state_id() == ev::din::StateID::SessionStop);

    message_din::SessionStopResponse session_stop{};
    session_stop.header = header();
    session_stop.response_code = dt::ResponseCode::OK;
    primed.handle_response(session_stop);
    primed.feed(ev::din::Event::V2GTP_MESSAGE);
    REQUIRE(primed.ctx.is_session_stopped());
    return primed.ctx.is_session_paused();
}
