// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <everest/util/async/monitor.hpp>
#include <everest/util/fsm/fsm.hpp>

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>
#include <iso15118/ev/params/dc.hpp>
#include <iso15118/ev/params/iso2_ac.hpp>
#include <iso15118/ev/session/latches.hpp>
#include <iso15118/message_2/power_delivery.hpp>
#include <iso15118/message_2/session_stop.hpp>
#include <iso15118/message_2/type.hpp>
#include <iso15118/message_2/variant.hpp>
#include <iso15118/message_2/welding_detection.hpp>

using namespace iso15118;

inline constexpr message_2::datatypes::SessionId D2_SESSION_ID{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02};
inline constexpr message_2::datatypes::SessionId D2_WRONG_SESSION_ID{0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0};

inline message_2::Header d2_header(const message_2::datatypes::SessionId& id = D2_SESSION_ID) {
    message_2::Header header;
    header.session_id = id;
    return header;
}

// A response echoing D2_SESSION_ID with ResponseCode OK; every other field is value-initialised.
template <typename Res> Res d2_ok() {
    Res res{};
    res.header = d2_header();
    res.response_code = message_2::datatypes::ResponseCode::OK;
    return res;
}

// DC parameters every DC fixture starts from unless the test names its own.
inline ev::params::DcChargeParams default_dc_params() {
    ev::params::DcChargeParams params;
    params.max_charge_power = 60000.0f;
    params.max_charge_current = 150.0f;
    params.max_voltage = 900.0f;
    params.min_voltage = 200.0f;
    params.energy_capacity = 80000.0f;
    params.target_voltage = 400.0f;
    params.target_current = 100.0f;
    params.present_soc = 50.0;
    params.present_voltage = 400.0f;
    return params;
}

inline ev::params::EvSessionParams ac_params() {
    ev::params::EvSessionParams params;
    params.energy_transfer_mode = message_2::datatypes::EnergyTransferMode::AC_three_phase_core;
    return params;
}

class D2StateHelper {
public:
    explicit D2StateHelper(const ev::feedback::Callbacks& callbacks, ev::params::EvSessionParams params = {},
                           bool has_cp_state_feedback = false,
                           std::optional<message_2::datatypes::SessionId> resumed_session_id = std::nullopt,
                           ev::config::WeldingDetectionConfig welding_detection = {}) :
        ctx(callbacks, msg_exch, std::move(params), latches, dc_params, has_cp_state_feedback, resumed_session_id,
            welding_detection) {
    }

    ev::d2::Context& get_context();

    ev::d2::MessageExchange& get_message_exchange() {
        return msg_exch;
    }

    // Mirrors the Session: the request slot is empty by the time a response arrives.
    template <typename ResponseType> void handle_response(const ResponseType& response) {
        while (msg_exch.has_request()) {
            msg_exch.take_request();
        }
        msg_exch.set_response(std::make_unique<message_2::ResponseVariant>(response));
    }

    void set_dc_params(const ev::params::DcChargeParams& params) {
        auto h = dc_params.handle();
        *h = params;
    }

    // What Session::deliver_control_event does before it feeds the event.
    void latch(const ev::d20::ControlEvent& event) {
        ev::session::latch(latches, event);
    }

private:
    ev::d2::MessageExchange msg_exch{};
    everest::lib::util::monitor<ev::params::DcChargeParams> dc_params{ev::params::DcChargeParams{}};
    ev::session::Latches latches{};

    ev::d2::Context ctx;
};

// Pending requests, EXI round-trip-decoded the way the Session transmits them.
class DecodedRequests {
public:
    template <typename Msg> std::optional<Msg> get() const {
        for (const auto& variant : variants) {
            if (const auto* msg = variant->get_if<Msg>()) {
                return *msg;
            }
        }
        return std::nullopt;
    }

    std::vector<message_2::Type> types() const {
        std::vector<message_2::Type> out;
        out.reserve(variants.size());
        for (const auto& variant : variants) {
            out.push_back(variant->get_type());
        }
        return out;
    }

    bool empty() const {
        return variants.empty();
    }

    void add(std::unique_ptr<message_2::Variant> variant) {
        variants.push_back(std::move(variant));
    }

private:
    std::vector<std::unique_ptr<message_2::Variant>> variants;
};

// Pop and decode every pending request (destructive, FIFO).
DecodedRequests take_all_requests(ev::d2::MessageExchange& msg_exch);

// A freshly generated secp256r1 private key in PEM, for the Plug & Charge signing paths.
std::string make_test_ec_key_pem();

inline const auto d2_no_seed = [](D2StateHelper&) {};

// Feeds a fresh Pred `response` and returns the state its transition created, with the first request
// that transition sent pending.
template <typename Pred, typename Response, typename... Args>
ev::d2::BasePointerType d2_successor_of(D2StateHelper& helper, const Response& response, Args&&... args) {
    auto pred = helper.get_context().create_state<Pred>(std::forward<Args>(args)...);
    helper.handle_response(response);
    auto result = pred->feed(ev::d2::Event::V2GTP_MESSAGE);
    REQUIRE(result.output == ev::d2::Disposition::Transitioning);
    REQUIRE(result.new_state != nullptr);
    return std::move(result.new_state);
}

// Passed as a PrimedState's only state argument: the state is entered through the transition
// `enter(helper)` performs instead of being created bare.
template <typename Enter> struct D2EnteredBy {
    Enter enter;
};

template <typename Enter> D2EnteredBy<Enter> d2_entered_by(Enter enter) {
    return {std::move(enter)};
}

template <typename T> struct IsD2EnteredBy : std::false_type {};
template <typename Enter> struct IsD2EnteredBy<D2EnteredBy<Enter>> : std::true_type {};

// A primed fixture: session id set to D2_SESSION_ID, `seed(helper)` run, then State entered. Created
// bare, nothing is pending: a state's first request goes out on the transition into it. Entered
// through a D2EnteredBy, the request that transition sent is pending.
template <typename State> struct PrimedState {
    template <typename Seed, typename... Args>
    PrimedState(const ev::feedback::Callbacks& callbacks, ev::config::WeldingDetectionConfig welding, Seed seed,
                Args&&... args) :
        helper(callbacks, {}, false, std::nullopt, welding),
        ctx(helper.get_context()),
        fsm(seed_and_enter(seed, std::forward<Args>(args)...)) {
    }

    template <typename Seed, typename... Args, std::enable_if_t<std::is_invocable_v<Seed&, D2StateHelper&>, int> = 0>
    PrimedState(const ev::feedback::Callbacks& callbacks, Seed seed, Args&&... args) :
        PrimedState(callbacks, ev::params::EvSessionParams{}, false, seed, std::forward<Args>(args)...) {
    }

    // Session parameters and CP-state feedback are fixed at Context construction, so they go before the
    // seed rather than into it.
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

    auto feed(ev::d2::Event event) {
        return fsm.feed(event);
    }

    D2StateHelper helper;
    ev::d2::Context& ctx;
    fsm::v2::FSM<ev::d2::StateBase> fsm;

private:
    template <typename Seed, typename... Args> ev::d2::BasePointerType seed_and_enter(Seed& seed, Args&&... args) {
        ctx.set_session_id(D2_SESSION_ID);
        helper.set_dc_params(default_dc_params());
        seed(helper);
        if constexpr (sizeof...(Args) == 1 and (IsD2EnteredBy<std::decay_t<Args>>::value and ...)) {
            auto state = (args.enter(helper), ...);
            REQUIRE(dynamic_cast<State*>(state.get()) != nullptr);
            return state;
        } else {
            return ctx.create_state<State>(std::forward<Args>(args)...);
        }
    }
};

// A predecessor state fed outside an FSM, so no successor's enter() runs: whatever is pending after its
// feed was sent on the transition itself. The session id is D2_SESSION_ID and the DC parameters are
// the defaults; `seed(helper)` runs before the predecessor is created.
class D2Predecessor {
public:
    template <typename State, typename Seed, typename... Args>
    static D2Predecessor make(ev::params::EvSessionParams params, bool has_cp_state_feedback, Seed seed,
                              Args&&... args) {
        D2Predecessor pred(std::move(params), has_cp_state_feedback);
        seed(*pred.helper);
        pred.current = pred.ctx.create_state<State>(std::forward<Args>(args)...);
        return pred;
    }

    template <typename State, typename... Args> static D2Predecessor of(Args&&... args) {
        return make<State>(ev::params::EvSessionParams{}, false, d2_no_seed, std::forward<Args>(args)...);
    }

    // Feeds `response`, requires a Transitioning result into `successor`, and returns what the
    // transition sent. The successor's enter() then runs and must send nothing more; it is the state
    // the next feed reaches.
    template <typename Response> DecodedRequests transitions_to(ev::d2::StateID successor, const Response& response) {
        auto result = feed(response);
        REQUIRE(result.output == ev::d2::Disposition::Transitioning);
        return enter(successor, std::move(result.new_state));
    }

    // Feeds `response`, requires a Waiting result into `successor`, which holds its first request:
    // nothing is pending before or after its enter(). It is the state the next feed reaches.
    template <typename Response> void waits_in(ev::d2::StateID successor, const Response& response) {
        auto result = feed(response);
        REQUIRE(result.output == ev::d2::Disposition::Waiting);
        REQUIRE(result.new_state != nullptr);
        REQUIRE(result.new_state->get_id() == successor);
        REQUIRE_FALSE(helper->get_message_exchange().has_request());
        current = std::move(result.new_state);
        current->enter();
        REQUIRE_FALSE(helper->get_message_exchange().has_request());
    }

    // Delivers a control event to the current state.
    ev::d2::Result control(const ev::d20::ControlEvent& event) {
        helper->latch(event);
        return current->feed(ev::d2::Event::CONTROL_MESSAGE);
    }

    // Feeds `response` and requires a Stopping result with a stopped session and nothing pending.
    template <typename Response> void stops_without_request(const Response& response) {
        const auto result = feed(response);
        REQUIRE(result.output == ev::d2::Disposition::Stopping);
        REQUIRE(result.new_state == nullptr);
        REQUIRE(ctx.is_session_stopped());
        REQUIRE_FALSE(helper->get_message_exchange().has_request());
    }

    std::unique_ptr<D2StateHelper> helper;
    ev::d2::Context& ctx;

private:
    D2Predecessor(ev::params::EvSessionParams params, bool has_cp_state_feedback) :
        helper(std::make_unique<D2StateHelper>(no_callbacks, std::move(params), has_cp_state_feedback)),
        ctx(helper->get_context()) {
        ctx.set_session_id(D2_SESSION_ID);
        helper->set_dc_params(default_dc_params());
    }

    template <typename Response> ev::d2::Result feed(const Response& response) {
        helper->handle_response(response);
        return current->feed(ev::d2::Event::V2GTP_MESSAGE);
    }

    DecodedRequests enter(ev::d2::StateID successor, ev::d2::BasePointerType state) {
        REQUIRE(state != nullptr);
        REQUIRE(state->get_id() == successor);
        auto sent = take_all_requests(helper->get_message_exchange());
        current = std::move(state);
        current->enter();
        REQUIRE_FALSE(helper->get_message_exchange().has_request());
        return sent;
    }

    inline static const ev::feedback::Callbacks no_callbacks{};
    ev::d2::BasePointerType current;
};

// Feed `response` and assert the stop tail every negative path shares.
template <typename ResponseType>
void expect_stops_session(D2StateHelper& helper, fsm::v2::FSM<ev::d2::StateBase>& fsm, const ResponseType& response,
                          ev::d2::StateID expected_id) {
    helper.handle_response(response);
    const auto result = fsm.feed(ev::d2::Event::V2GTP_MESSAGE);
    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == expected_id);
    REQUIRE(helper.get_context().is_session_stopped() == true);
}

template <typename Primed, typename ResponseType>
void expect_stops_session(Primed& primed, const ResponseType& response, ev::d2::StateID expected_id) {
    expect_stops_session(primed.helper, primed.fsm, response, expected_id);
}

// FAILED response code, wrong-variant response, non-echoed session id: the three rejections every
// response-consuming state shares. `make_fsm(helper)` builds the entered FSM, `make_ok(header)` the
// state's otherwise-valid response.
template <typename MakeFsm, typename MakeOk, typename WrongVariant>
void check_rejection_paths(const ev::feedback::Callbacks& callbacks, ev::d2::StateID expected_id, MakeFsm make_fsm,
                           MakeOk make_ok, const WrongVariant& wrong_variant,
                           ev::params::EvSessionParams params = ev::params::EvSessionParams{}) {
    const auto run = [&](const auto& response) {
        D2StateHelper helper{callbacks, params};
        helper.set_dc_params(default_dc_params());
        auto fsm = make_fsm(helper);
        expect_stops_session(helper, fsm, response, expected_id);
    };

    SECTION("stops the session on a FAILED response code") {
        auto res = make_ok(d2_header());
        res.response_code = message_2::datatypes::ResponseCode::FAILED;
        run(res);
    }
    SECTION("stops the session on a wrong-variant response") {
        run(wrong_variant);
    }
    SECTION("stops the session on a mismatched response session_id") {
        run(make_ok(d2_header(D2_WRONG_SESSION_ID)));
    }
}

// What the graceful stop walk ended in: the reason SessionStopReq carried, and whether the
// acknowledged SessionStop left the session joinable.
struct StopWalk {
    message_2::datatypes::ChargingSession charging_session;
    bool paused;
};

// From a pending PowerDelivery(Stop): accept it, pass DC welding detection at a safe voltage, and
// accept the SessionStop the walk ends in.
template <typename Primed> StopWalk walk_stop(Primed& primed) {
    namespace dt = message_2::datatypes;
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PowerDelivery);
    message_2::PowerDeliveryResponse power_delivery{};
    power_delivery.header = d2_header();
    power_delivery.response_code = dt::ResponseCode::OK;
    primed.handle_response(power_delivery);
    primed.feed(ev::d2::Event::V2GTP_MESSAGE);
    if (primed.fsm.get_current_state_id() == ev::d2::StateID::WeldingDetection) {
        message_2::WeldingDetectionResponse welding{};
        welding.header = d2_header();
        welding.response_code = dt::ResponseCode::OK;
        welding.evse_present_voltage = dt::to_physical_value(0.0, dt::Unit::V);
        primed.handle_response(welding);
        primed.feed(ev::d2::Event::V2GTP_MESSAGE);
    }
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
    const auto request = primed.take_requests().template get<message_2::SessionStopRequest>();
    REQUIRE(request.has_value());

    message_2::SessionStopResponse session_stop{};
    session_stop.header = d2_header();
    session_stop.response_code = dt::ResponseCode::OK;
    primed.handle_response(session_stop);
    primed.feed(ev::d2::Event::V2GTP_MESSAGE);
    REQUIRE(primed.ctx.is_session_stopped());
    return {request->charging_session, primed.ctx.is_session_paused()};
}
