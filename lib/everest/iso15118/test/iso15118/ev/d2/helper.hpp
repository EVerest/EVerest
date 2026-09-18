// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
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
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/session_params.hpp>
#include <iso15118/message_2/type.hpp>
#include <iso15118/message_2/variant.hpp>

using namespace iso15118;

inline constexpr message_2::datatypes::SessionId D2_SESSION_ID{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02};
inline constexpr message_2::datatypes::SessionId D2_WRONG_SESSION_ID{0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0};

inline message_2::Header d2_header(const message_2::datatypes::SessionId& id = D2_SESSION_ID) {
    message_2::Header header;
    header.session_id = id;
    return header;
}

// DC parameters every DC fixture starts from unless the test names its own.
inline ev::DcChargeParams default_dc_params() {
    ev::DcChargeParams params;
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

inline ev::EvSessionParams ac_params() {
    ev::EvSessionParams params;
    params.energy_transfer_mode = message_2::datatypes::EnergyTransferMode::AC_three_phase_core;
    return params;
}

class D2StateHelper {
public:
    explicit D2StateHelper(const ev::feedback::Callbacks& callbacks, ev::EvSessionParams params = {},
                           bool has_cp_state_feedback = false,
                           std::optional<message_2::datatypes::SessionId> resumed_session_id = std::nullopt) :
        ctx(callbacks, msg_exch, std::move(params), control_event, dc_params, has_cp_state_feedback,
            resumed_session_id) {
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
        msg_exch.set_response(std::make_unique<message_2::Variant>(response));
    }

    void set_dc_params(const ev::DcChargeParams& params) {
        auto h = dc_params.handle();
        *h = params;
    }

    void set_control_event(const ev::d20::ControlEvent& event) {
        control_event = event;
    }

    void clear_control_event() {
        control_event.reset();
    }

private:
    ev::d2::MessageExchange msg_exch{};
    everest::lib::util::monitor<ev::DcChargeParams> dc_params{ev::DcChargeParams{}};
    std::optional<ev::d20::ControlEvent> control_event{};

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

// A primed fixture: session id set to D2_SESSION_ID, `seed(helper)` run, then State entered. The entry
// request queued by State::enter() is already pending.
template <typename State> struct PrimedState {
    template <typename Seed, typename... Args, std::enable_if_t<std::is_invocable_v<Seed&, D2StateHelper&>, int> = 0>
    PrimedState(const ev::feedback::Callbacks& callbacks, Seed seed, Args&&... args) :
        PrimedState(callbacks, ev::EvSessionParams{}, false, seed, std::forward<Args>(args)...) {
    }

    // Session parameters and CP-state feedback are fixed at Context construction, so they go before the
    // seed rather than into it.
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
        return ctx.create_state<State>(std::forward<Args>(args)...);
    }
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
                           ev::EvSessionParams params = ev::EvSessionParams{}) {
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
