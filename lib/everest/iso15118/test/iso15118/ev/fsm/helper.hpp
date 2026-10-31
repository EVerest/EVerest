// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <everest/util/async/monitor.hpp>
#include <everest/util/fsm/fsm.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/states.hpp>
#include <iso15118/ev/params/ac.hpp>
#include <iso15118/ev/params/dc.hpp>
#include <iso15118/ev/session/latches.hpp>
#include <iso15118/io/log_levels.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/message/dc_welding_detection.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/variant.hpp>

using namespace iso15118;

inline const auto SESSION_HEADER =
    message_20::Header{std::array<uint8_t, 8>{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};
inline const auto WRONG_HEADER =
    message_20::Header{std::array<uint8_t, 8>{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00}, 1691411798};

// SECC time far from local UTC: an SECC epoch at power-on [V2G20-1532], five seconds ago.
inline constexpr std::uint64_t SECC_REFERENCE_US = 5'000'000;

// `secc_time` was read from a clock synchronized to `reference` no earlier than `since`.
inline void require_tracks_reference(std::uint64_t secc_time, std::uint64_t reference,
                                     std::chrono::steady_clock::time_point since) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - since);
    REQUIRE(secc_time >= reference);
    // + 1: a header stamp is bumped past the reference itself.
    REQUIRE(secc_time <= reference + static_cast<std::uint64_t>(elapsed.count()) + 1);
}

// Collects libiso15118 log lines while alive. Its destructor installs a printing callback, so a later
// test neither logs into freed storage nor runs with the library logger silenced.
class LogCapture {
public:
    LogCapture() {
        io::set_logging_callback(
            [this](LogLevel level, std::string message) { lines.emplace_back(level, std::move(message)); });
    }

    ~LogCapture() {
        io::set_logging_callback([](LogLevel level, const std::string& message) {
            std::printf("log(%d): %s\n", static_cast<int>(level), message.c_str());
        });
    }

    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    std::vector<std::string> messages(LogLevel level) const {
        std::vector<std::string> out;
        for (const auto& [line_level, message] : lines) {
            if (line_level == level) {
                out.push_back(message);
            }
        }
        return out;
    }

    bool has_warning_containing(const std::string& needle) const {
        for (const auto& message : messages(LogLevel::Warning)) {
            if (message.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

private:
    std::vector<std::pair<LogLevel, std::string>> lines;
};

// What a fixture advertises unless the test names its own list. Named so PrimedState
// can reach the third constructor argument without restating it.
inline const std::vector<message_20::SupportedAppProtocol> DEFAULT_APP_PROTOCOLS = {
    {"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1}};

// The DER configuration reaches the FSM through SessionOptions; tests still name the two
// values separately, so fold them in here.
inline ev::d20::SessionOptions with_der(ev::d20::SessionOptions options, ev::d20::der::DerControlFunctions functions,
                                        bool stop_on_unsupported) {
    options.d20.der_control_functions = functions;
    options.d20.der_stop_on_unsupported_functions = stop_on_unsupported;
    return options;
}

class FsmStateHelper {
public:
    // The DER configuration travels inside SessionOptions; this overload keeps whatever the
    // caller put there rather than overwriting it with the defaults below.
    FsmStateHelper(
        const ev::feedback::Callbacks& callbacks,
        std::vector<message_20::SupportedAppProtocol> protocols = DEFAULT_APP_PROTOCOLS,
        message_20::datatypes::ServiceCategory requested_service = message_20::datatypes::ServiceCategory::DC,
        ev::d20::SessionOptions options = {}) :
        advertised_app_protocols(std::move(protocols)),
        ctx(callbacks, msg_exch, evcc_id, advertised_app_protocols, latches, dc_params, ac_params, requested_service,
            std::move(options)) {
    }

    // Tests that name the two DER values separately rather than building SessionOptions.
    FsmStateHelper(const ev::feedback::Callbacks& callbacks, std::vector<message_20::SupportedAppProtocol> protocols,
                   message_20::datatypes::ServiceCategory requested_service,
                   ev::d20::der::DerControlFunctions der_control_functions, bool der_stop_on_unsupported_functions,
                   ev::d20::SessionOptions options = {}) :
        FsmStateHelper(callbacks, std::move(protocols), requested_service,
                       with_der(std::move(options), der_control_functions, der_stop_on_unsupported_functions)) {
    }

    ev::d20::Context& get_context();

    ev::d20::MessageExchange& get_message_exchange() {
        return msg_exch;
    }

    template <typename ResponseType> void handle_response(const ResponseType& response) {
        // Mirrors the Session: the request slot is empty by the time a response
        // arrives, so drain it here for the request the feed sends.
        while (msg_exch.has_request()) {
            msg_exch.take_request();
        }
        msg_exch.set_response(std::make_unique<message_20::ResponseVariant>(response));
    }

    // Seed the module -> FSM DcChargeParams channel before creating a state.
    void set_dc_params(const ev::params::DcChargeParams& params) {
        auto h = dc_params.handle();
        *h = params;
    }

    // Direct access to the module -> FSM DcChargeParams channel (for tests that mutate
    // live fields through the monitor handle or observe it under concurrency).
    everest::lib::util::monitor<ev::params::DcChargeParams>& get_dc_params_monitor() {
        return dc_params;
    }

    // Seed the module -> FSM AcChargeParams channel before creating a state.
    void set_ac_params(const ev::params::AcChargeParams& params) {
        auto h = ac_params.handle();
        *h = params;
    }

    // Direct access to the module -> FSM AcChargeParams channel.
    everest::lib::util::monitor<ev::params::AcChargeParams>& get_ac_params_monitor() {
        return ac_params;
    }

    // What Session::deliver_control_event does before it feeds the event.
    void latch(const ev::d20::ControlEvent& event) {
        ev::session::latch(latches, event);
    }

private:
    ev::d20::MessageExchange msg_exch{};

    everest::lib::util::monitor<ev::params::DcChargeParams> dc_params{ev::params::DcChargeParams{}};

    everest::lib::util::monitor<ev::params::AcChargeParams> ac_params{ev::params::AcChargeParams{}};

    message_20::datatypes::Identifier evcc_id{"EVTESTID01"};
    // Always set from the constructor argument, which defaults to DEFAULT_APP_PROTOCOLS.
    std::vector<message_20::SupportedAppProtocol> advertised_app_protocols;
    ev::session::Latches latches{};

    ev::d20::Context ctx;
};

// A steady clock the test moves by hand.
inline std::chrono::steady_clock::time_point hand_steady_time{std::chrono::hours(1)};

inline std::chrono::steady_clock::time_point hand_steady_now() {
    return hand_steady_time;
}

// Puts the Context's secc_clock on hand_steady_now, which restarts at a fixed time.
inline void use_hand_steady_clock(FsmStateHelper& helper) {
    hand_steady_time = std::chrono::steady_clock::time_point{std::chrono::hours(1)};
    helper.get_context().secc_clock() = ev::d20::SeccClock{ev::d20::SeccClock::system_now_us, hand_steady_now};
}

// start_charging asked for `energy_wh` and to leave `departure` after use_hand_steady_clock's start.
inline ev::d20::SessionOptions with_energy_request(float energy_wh, std::chrono::seconds departure,
                                                   ev::d20::SessionOptions options = {}) {
    options.energy_request.energy_wh = energy_wh;
    options.energy_request.departure = std::chrono::steady_clock::time_point{std::chrono::hours(1)} + departure;
    return options;
}

// Pending requests, EXI round-trip-decoded the way the Session transmits them;
// proves a request actually serializes, not just that a retained copy matches.
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

    void add(std::unique_ptr<message_20::RequestVariant> variant) {
        variants.push_back(std::move(variant));
    }

private:
    std::vector<std::unique_ptr<message_20::RequestVariant>> variants;
};

// Pop and decode every pending request from the exchange (destructive, FIFO).
DecodedRequests take_all_requests(ev::d20::MessageExchange& msg_exch);

// A SupportedAppProtocolResponse accepting schema_id 1, staged the way the Session stages it.
std::unique_ptr<message_20::ResponseVariant> make_sap_ok_response();

// A no-op context seed: the default for states that only need the primed session id.
inline const auto no_seed = [](FsmStateHelper&) {};

// Feeds a fresh Pred `response` and returns the state its transition created, with the first
// request that transition sent pending.
template <typename Pred, typename Response, typename... Args>
ev::d20::BasePointerType successor_of(FsmStateHelper& helper, const Response& response, Args&&... args) {
    auto pred = helper.get_context().create_state<Pred>(std::forward<Args>(args)...);
    helper.handle_response(response);
    auto result = pred->feed(ev::d20::Event::V2GTP_MESSAGE);
    REQUIRE(result.output == ev::d20::Disposition::Transitioning);
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

// A primed FSM fixture: primes the session id to SESSION_HEADER (so response echoes
// line up), runs `seed(helper)` before the state is entered, then enters State. Created
// bare, nothing is pending: a state's first request goes out on the transition into it.
// Entered through an EnteredBy, the request that transition sent is pending.
// The Context requests DC unless the test names another service; the service is fixed
// at construction, so it goes before the seed rather than into it.
template <typename State> struct PrimedState {
    // Seed must be callable, which is what keeps this from competing with the overload
    // below when a test does name a service.
    template <typename Seed, typename... Args, std::enable_if_t<std::is_invocable_v<Seed&, FsmStateHelper&>, int> = 0>
    PrimedState(const ev::feedback::Callbacks& callbacks, Seed seed, Args&&... args) :
        PrimedState(callbacks, message_20::datatypes::ServiceCategory::DC, ev::d20::SessionOptions{}, seed,
                    std::forward<Args>(args)...) {
    }

    template <typename Seed, typename... Args, std::enable_if_t<std::is_invocable_v<Seed&, FsmStateHelper&>, int> = 0>
    PrimedState(const ev::feedback::Callbacks& callbacks, message_20::datatypes::ServiceCategory requested_service,
                Seed seed, Args&&... args) :
        PrimedState(callbacks, requested_service, ev::d20::SessionOptions{}, seed, std::forward<Args>(args)...) {
    }

    // Session options the Context only takes at construction (preferred control mode, CP feedback).
    template <typename Seed, typename... Args>
    PrimedState(const ev::feedback::Callbacks& callbacks, message_20::datatypes::ServiceCategory requested_service,
                ev::d20::SessionOptions options, Seed seed, Args&&... args) :
        helper(callbacks, DEFAULT_APP_PROTOCOLS, requested_service, std::move(options)),
        ctx(helper.get_context()),
        fsm(seed_and_enter(seed, std::forward<Args>(args)...)) {
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
        if constexpr (sizeof...(Args) == 1 and (IsEnteredBy<std::decay_t<Args>>::value and ...)) {
            auto state = (args.enter(helper), ...);
            REQUIRE(dynamic_cast<State*>(state.get()) != nullptr);
            return state;
        } else {
            return ctx.create_state<State>(std::forward<Args>(args)...);
        }
    }
};

// Feed `response` and assert the stop tail every negative path shares: no
// transition, still `expected_id`, session stop requested. For tests needing a
// non-default FsmStateHelper ctor, so can't use PrimedState.
template <typename ResponseType>
void expect_stops_session(FsmStateHelper& helper, fsm::v2::FSM<ev::d20::StateBase>& fsm, const ResponseType& response,
                          ev::d20::StateID expected_id) {
    helper.handle_response(response);
    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);
    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == expected_id);
    REQUIRE(helper.get_context().is_session_stopped() == true);
}

// Same stop tail, driven through a PrimedState.
template <typename Primed, typename ResponseType>
void expect_stops_session(Primed& primed, const ResponseType& response, ev::d20::StateID expected_id) {
    expect_stops_session(primed.helper, primed.fsm, response, expected_id);
}

// The three rejection checks shared by every response-consuming state: FAILED
// response code, wrong-variant response, and a non-echoed session id.
// `make_fsm(helper)` builds the entered FSM; `make_ok(header)` builds the state's
// otherwise-valid OK response.
template <typename MakeFsm, typename MakeOk, typename WrongVariant>
void check_rejection_paths(const ev::feedback::Callbacks& callbacks, ev::d20::StateID expected_id, MakeFsm make_fsm,
                           MakeOk make_ok, const WrongVariant& wrong_variant) {
    const auto run = [&](const auto& response) {
        FsmStateHelper helper{callbacks};
        auto fsm = make_fsm(helper);
        expect_stops_session(helper, fsm, response, expected_id);
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

// A predecessor state fed outside an FSM, so no successor's enter() runs: whatever is pending after
// its feed was sent on the transition itself.
class Predecessor {
public:
    template <typename State, typename... Args>
    static Predecessor make(const ev::feedback::Callbacks& callbacks, message_20::datatypes::ServiceCategory service,
                            ev::d20::SessionOptions options, Args&&... args) {
        return Predecessor(callbacks, service, std::move(options),
                           [&](ev::d20::Context& ctx) { return ctx.create_state<State>(std::forward<Args>(args)...); });
    }

    template <typename State, typename... Args>
    static Predecessor in(message_20::datatypes::ServiceCategory service, ev::d20::SessionOptions options,
                          Args&&... args) {
        return make<State>(no_callbacks, service, std::move(options), std::forward<Args>(args)...);
    }

    template <typename State, typename... Args> static Predecessor dc(Args&&... args) {
        return in<State>(message_20::datatypes::ServiceCategory::DC, ev::d20::SessionOptions{},
                         std::forward<Args>(args)...);
    }

    // A DC predecessor `create` builds from the Context, for a state only a factory makes.
    template <typename Create> static Predecessor dc_from(Create create) {
        return Predecessor(no_callbacks, message_20::datatypes::ServiceCategory::DC, ev::d20::SessionOptions{},
                           std::move(create));
    }

    // Feeds a response the predecessor answers by staying, e.g. an Ongoing re-poll.
    template <typename Response> void stays_on(const Response& response) {
        helper->handle_response(response);
        const auto result = current->feed(ev::d20::Event::V2GTP_MESSAGE);
        REQUIRE(result.output == ev::d20::Disposition::Awaiting);
        REQUIRE(result.new_state == nullptr);
    }

    // Feeds `response`, requires a Transitioning result into `successor`, and returns what the
    // transition sent. The successor's enter() then runs and must send nothing more.
    template <typename Response> DecodedRequests transitions_to(ev::d20::StateID successor, const Response& response) {
        helper->handle_response(response);
        return feed_into(successor);
    }

    DecodedRequests feed_into(ev::d20::StateID successor) {
        auto result = current->feed(ev::d20::Event::V2GTP_MESSAGE);
        REQUIRE(result.output == ev::d20::Disposition::Transitioning);
        REQUIRE(result.new_state != nullptr);
        REQUIRE(result.new_state->get_id() == successor);

        auto sent = take_all_requests(helper->get_message_exchange());
        result.new_state->enter();
        REQUIRE_FALSE(helper->get_message_exchange().has_request());
        return sent;
    }

    std::unique_ptr<FsmStateHelper> helper;
    ev::d20::Context& ctx;

private:
    template <typename Create>
    Predecessor(const ev::feedback::Callbacks& callbacks, message_20::datatypes::ServiceCategory service,
                ev::d20::SessionOptions options, Create create) :
        helper(std::make_unique<FsmStateHelper>(callbacks, DEFAULT_APP_PROTOCOLS, service, std::move(options))),
        ctx(helper->get_context()) {
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        current = create(ctx);
    }

    inline static const ev::feedback::Callbacks no_callbacks{};
    ev::d20::BasePointerType current;
};

// What the graceful stop walk ended in: the reason SessionStopReq carried, and whether the
// acknowledged SessionStop left the session joinable.
struct StopWalk {
    message_20::datatypes::ChargingSession charging_session;
    bool paused;
};

// From a pending PowerDelivery(Stop): accept it, pass DC welding detection at a safe voltage when
// the service needs it, and accept the SessionStop the walk ends in.
template <typename Primed> StopWalk walk_stop(Primed& primed) {
    using message_20::datatypes::ResponseCode;
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    primed.handle_response(message_20::PowerDeliveryResponse{SESSION_HEADER, ResponseCode::OK, std::nullopt});
    primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    // Ongoing, then the closing Finished request.
    for (int i = 0; i < 2 and primed.fsm.get_current_state_id() == ev::d20::StateID::DC_WeldingDetection; ++i) {
        primed.handle_response(message_20::DC_WeldingDetectionResponse{SESSION_HEADER, ResponseCode::OK,
                                                                       message_20::datatypes::RationalNumber{0, 0}});
        primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    }
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    const auto request = primed.take_requests().template get<message_20::SessionStopRequest>();
    REQUIRE(request.has_value());

    primed.handle_response(message_20::SessionStopResponse{SESSION_HEADER, ResponseCode::OK});
    primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    REQUIRE(primed.ctx.is_session_stopped());
    return {request->charging_session, primed.ctx.is_session_paused()};
}
