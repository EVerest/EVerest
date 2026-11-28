// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <everest/util/fsm/fsm.hpp>

#include <iso15118/d20/context.hpp>
#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/ev_information.hpp>
#include <iso15118/d20/states.hpp>
#include <iso15118/d20/timeout.hpp>

#include <iso15118/io/sha_hash.hpp>
#include <iso15118/message/supported_app_protocol.hpp>

#include <iso15118/session/config.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/secc_engine.hpp>

namespace iso15118 {

// ISO 15118-20 SECC engine: wraps the d20 server state machine starting at SessionSetup.
class D20SeccEngine {
public:
    D20SeccEngine(io::StreamOutputView output_view, session::SessionConfig config,
                  std::optional<d20::PauseContext>& pause_ctx, session::feedback::Callbacks callbacks,
                  d20::Timeouts& timeouts, const d20::EVSupportedAppProtocols& offered_protocols,
                  const message_20::SupportedAppProtocol& selected_protocol,
                  std::optional<io::sha512_hash_t> vehicle_cert_hash, bool tls_active,
                  bool skip_app_protocol_negotiation = false);

    bool on_packet(v2gtp::PayloadType, const io::StreamInputView&);
    void on_control_event(const d20::ControlEvent&);
    void on_timeout(d20::TimeoutType);

    bool has_outgoing() const;
    V2gMessageType peek_response_type() const {
        return message_exchange.peek_response_type();
    }
    std::optional<SeccOutgoing> take_outgoing();

    // Includes a terminal response that is still pending on the wire.
    bool is_stopping() const {
        return ctx.session_stopped or ctx.session_paused;
    }
    // [V2G20-2010], [V2G20-2012], [V2G20-2013], AMD1 [V2G20-3208] and [V2G20-3209] (which replace [V2G20-2009] and
    // [V2G20-2192]): the V2G_SECC_Sequence_Timeout of Tables 216 and 217 applies while a ChargeLoopReq is the next
    // request.
    bool expects_charge_loop_request() const {
        switch (fsm.get_current_state_id()) {
        case d20::StateID::AC_ChargeLoop:
        case d20::StateID::DC_ChargeLoop:
        case d20::StateID::AC_DER_IEC_ChargeLoop:
        case d20::StateID::AC_DER_SAE_ChargeLoop:
            return not is_stopping();
        default:
            return false;
        }
    }
    bool is_finished() const;
    bool is_paused() const;
    std::optional<session::feedback::SessionStopAction> pop_session_stop_res_pending();

    // ISO 15118-20 has no equivalent of the DIN [V2G-DC-962] / CP State A error termination handled
    // outside a response, so the session always ends through the regular EV-first close linger.
    bool is_finished_with_error() const {
        return false;
    }

    void request_shutdown();

    // ISO 15118-20 EVs cope with an immediate response; no pacing needed.
    bool delay_response_after_request() const {
        return false;
    }

private:
    d20::MessageExchange message_exchange;
    std::optional<d20::ControlEvent> active_control_event{std::nullopt};
    d20::Context ctx;
    fsm::v2::FSM<d20::StateBase> fsm;
};

} // namespace iso15118
