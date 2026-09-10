// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <everest/util/fsm/fsm.hpp>

#include <iso15118/io/sdp.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/v2g_message_type.hpp>

#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/states.hpp>

namespace iso15118::ev::d20 {

// Result of one FSM feed as the Session sees it.
struct FeedOutcome {
    Disposition output{Disposition::Ignored};
    bool transitioned{false};
    StateID state_before{StateID::SupportedAppProtocol};
    // Declared disposition not matched by what the state did; nullptr when consistent.
    const char* violation{nullptr};
};

// ISO 15118-20 engine: MessageExchange + Context + FSM. Owned by the Session, one per protocol
// generation; the SupportedAppProtocol handshake runs here for every session.
class Engine {
public:
    Engine(feedback::Callbacks callbacks, message_20::datatypes::Identifier evcc_id,
           std::vector<message_20::SupportedAppProtocol> advertised_app_protocols,
           const std::optional<ControlEvent>& current_control_event,
           everest::lib::util::monitor<DcChargeParams>& dc_params,
           everest::lib::util::monitor<AcChargeParams>& ac_params,
           message_20::datatypes::ServiceCategory energy_service, DerControlFunctions der_control_functions,
           bool der_stop_on_unsupported_functions, SessionOptions options);

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Constructs the FSM; the initial state's enter() queues the SAP request.
    void start();
    bool started() const;

    // Decodes @p view by protocol context and stages it as the pending response.
    void stage_response(io::v2gtp::PayloadType payload_type, const io::StreamInputView& view);
    V2gMessageType peek_response_type() const;

    FeedOutcome feed(Event ev);

    bool has_request() const;
    std::optional<std::pair<std::vector<uint8_t>, io::v2gtp::PayloadType>> take_request();
    void discard_request();

    // Response watchdog for a request sent from the current state.
    std::chrono::milliseconds response_timeout() const;
    // Bound for the current state's Ongoing polling loop, if it has one.
    std::optional<std::chrono::milliseconds> ongoing_timeout() const;

    std::optional<StateID> current_state() const;

    Context& context() {
        return ctx;
    }
    const Context& context() const {
        return ctx;
    }

private:
    MessageExchange message_exchange;
    Context ctx;
    std::optional<fsm::v2::FSM<StateBase>> fsm;
};

} // namespace iso15118::ev::d20
