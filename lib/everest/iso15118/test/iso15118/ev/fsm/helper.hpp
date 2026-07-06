// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <everest/util/async/monitor.hpp>
#include <everest/util/fsm/fsm.hpp>
#include <iso15118/ev/ac_charge_params.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/states.hpp>
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/variant.hpp>
#include <iso15118/session/logger.hpp>

using namespace iso15118;

inline constexpr auto SESSION_HEADER =
    message_20::Header{std::array<uint8_t, 8>{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};
inline constexpr auto WRONG_HEADER =
    message_20::Header{std::array<uint8_t, 8>{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00}, 1691411798};

class FsmStateHelper {
public:
    FsmStateHelper(
        const ev::feedback::Callbacks& callbacks,
        std::vector<message_20::SupportedAppProtocol> protocols = {{"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1}},
        message_20::datatypes::ServiceCategory requested_service = message_20::datatypes::ServiceCategory::DC) :
        advertised_app_protocols(std::move(protocols)),
        ctx(callbacks, msg_exch, logger, evcc_id, advertised_app_protocols, control_event, dc_params, ac_params,
            requested_service) {
        // Install a no-op session log callback so SessionLogger::event() does not throw bad_function_call
        // when state enter() invokes m_ctx.log.enter_state(...). Tests that need to capture log output
        // override this callback themselves and reset it at the end of the test case.
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});
    };

    ~FsmStateHelper() {
        // Reset the global session log callback so it does not leak across TEST_CASEs.
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});
    }

    ev::d20::Context& get_context();

    ev::d20::MessageExchange& get_message_exchange() {
        return msg_exch;
    }

    template <typename ResponseType> void handle_response(const ResponseType& response) {
        // Mirror the Session: by the time a response arrives, the request the current
        // state queued in enter() has already been transmitted (the single request
        // slot emptied). Take it here so the next state's enter() finds a free slot.
        while (msg_exch.has_request()) {
            msg_exch.take_request();
        }
        msg_exch.set_response(std::make_unique<message_20::Variant>(response));
    }

    // Seed the module -> FSM DcChargeParams channel before creating a state.
    void set_dc_params(const ev::DcChargeParams& params) {
        auto h = dc_params.handle();
        *h = params;
    }

    // Direct access to the module -> FSM DcChargeParams channel (for tests that mutate
    // live fields through the monitor handle or observe it under concurrency).
    everest::lib::util::monitor<ev::DcChargeParams>& get_dc_params_monitor() {
        return dc_params;
    }

    // Seed the module -> FSM AcChargeParams channel before creating a state.
    void set_ac_params(const ev::AcChargeParams& params) {
        auto h = ac_params.handle();
        *h = params;
    }

    // Direct access to the module -> FSM AcChargeParams channel.
    everest::lib::util::monitor<ev::AcChargeParams>& get_ac_params_monitor() {
        return ac_params;
    }

    // Set the active control event the Context reads via get_control_event<T>().
    void set_control_event(const ev::d20::ControlEvent& event) {
        control_event = event;
    }

    void clear_control_event() {
        control_event.reset();
    }

private:
    ev::d20::MessageExchange msg_exch{};

    everest::lib::util::monitor<ev::DcChargeParams> dc_params{ev::DcChargeParams{}};

    everest::lib::util::monitor<ev::AcChargeParams> ac_params{ev::AcChargeParams{}};

    iso15118::session::SessionLogger logger{this};

    message_20::datatypes::Identifier evcc_id{"EVTESTID01"};
    std::vector<message_20::SupportedAppProtocol> advertised_app_protocols{
        {"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1}};
    std::optional<ev::d20::ControlEvent> control_event{};

    ev::d20::Context ctx;
};

// The EV's pending requests, decoded the way the Session transmits them: each is popped
// via MessageExchange::take_request() and its EXI bytes round-trip-decoded. Asserting
// on a decoded request proves it actually serializes, not that a retained copy matches.
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
    std::vector<message_20::Type> types() const {
        std::vector<message_20::Type> out;
        out.reserve(variants.size());
        for (const auto& variant : variants) {
            out.push_back(variant->get_type());
        }
        return out;
    }

    bool empty() const {
        return variants.empty();
    }

    void add(std::unique_ptr<message_20::Variant> variant) {
        variants.push_back(std::move(variant));
    }

private:
    std::vector<std::unique_ptr<message_20::Variant>> variants;
};

// Pop and decode every pending request from the exchange (destructive, FIFO).
DecodedRequests take_all_requests(ev::d20::MessageExchange& msg_exch);

// A no-op context seed: the default for states that only need the primed session id.
inline const auto no_seed = [](FsmStateHelper&) {};

// A primed FSM fixture: owns the state helper, primes the session id to SESSION_HEADER
// (so response echoes line up), runs `seed(helper)` for per-state context (auth
// services, DcChargeParams, cert hashes) BEFORE the state is entered, then enters
// State with any forwarded ctor args. Access the FSM/context/exchange via the public
// members; the entry request queued by State::enter() is already pending.
template <typename State> struct PrimedState {
    template <typename Seed, typename... Args>
    PrimedState(const ev::feedback::Callbacks& callbacks, Seed seed, Args&&... args) :
        helper(callbacks), ctx(helper.get_context()), fsm(seed_and_enter(seed, std::forward<Args>(args)...)) {
    }

    template <typename ResponseType> void handle_response(const ResponseType& response) {
        helper.handle_response(response);
    }

    DecodedRequests take_requests() {
        return take_all_requests(helper.get_message_exchange());
    }

    auto feed(ev::d20::Event event) {
        return fsm.feed(event);
    }

    FsmStateHelper helper;
    ev::d20::Context& ctx;
    fsm::v2::FSM<ev::d20::StateBase> fsm;

private:
    template <typename Seed, typename... Args> ev::d20::BasePointerType seed_and_enter(Seed& seed, Args&&... args) {
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        seed(helper);
        return ctx.create_state<State>(std::forward<Args>(args)...);
    }
};

// The three structurally-identical rejection checks shared by every response-consuming
// state: it stops the session (and stays put) on a FAILED response code, on a
// wrong-variant response, and on a response whose session id does not echo the EV's.
// `make_fsm(helper)` seeds a freshly constructed helper and returns the entered FSM;
// `make_ok(header)` builds the state's otherwise-valid OK response for a given header
// (its response_code is overwritten to FAILED for that case). Sections keep per-case
// failure granularity.
template <typename MakeFsm, typename MakeOk, typename WrongVariant>
void check_rejection_paths(const ev::feedback::Callbacks& callbacks, ev::d20::StateID expected_id, MakeFsm make_fsm,
                           MakeOk make_ok, const WrongVariant& wrong_variant) {
    const auto run = [&](const auto& response) {
        FsmStateHelper helper{callbacks};
        auto fsm = make_fsm(helper);
        helper.handle_response(response);
        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);
        REQUIRE(result.transitioned() == false);
        REQUIRE(fsm.get_current_state_id() == expected_id);
        REQUIRE(helper.get_context().is_session_stopped() == true);
    };

    SECTION("stops the session on a FAILED response code") {
        auto res = make_ok(SESSION_HEADER);
        res.response_code = message_20::datatypes::ResponseCode::FAILED;
        run(res);
    }
    SECTION("stops the session on a wrong-variant response") {
        run(wrong_variant);
    }
    SECTION("stops the session on a mismatched response session_id") {
        run(make_ok(WRONG_HEADER));
    }
}
