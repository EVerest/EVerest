// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/d20/timeout.hpp>

#include <iso15118/message/supported_app_protocol.hpp>

#include <iso15118/session/config.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/protocol.hpp>
#include <iso15118/session/secc_engine.hpp>

namespace iso15118 {

// SupportedAppProtocol handshake engine (SECC side). It is the engine every session starts on: the
// handshake is protocol-independent, so it runs before the generation-specific engine exists and hands
// the session over to it via take_negotiated() once a protocol has been agreed. Skipping the handshake
// altogether (some EVs go straight to SessionSetupReq) therefore only means starting the Session on a
// protocol engine instead of this one.
//
// The handshake is a single request/response exchange, so there is no FSM here: the exchange either
// ends in a negotiated protocol (take_negotiated()) or the engine is finished and the session is torn
// down (is_finished()). See secc_engine.hpp for the engine contract.
class SapEngine {
public:
    // Outcome of a successful negotiation, consumed by the Session to construct the protocol engine.
    struct Negotiated {
        ProtocolId protocol_id;
        // Everything the EV offered and the entry we confirmed; the -20 engine reports both to the
        // module (ISO 15118-20 exposes the app-protocol list of the session).
        d20::EVSupportedAppProtocols offered_protocols;
        message_20::SupportedAppProtocol selected_protocol;
    };

    SapEngine(io::StreamOutputView output_view, const session::SessionConfig& config,
              session::feedback::Callbacks callbacks, bool tls_active);

    void on_packet(io::v2gtp::PayloadType, const io::StreamInputView&);
    void on_control_event(const d20::ControlEvent&);
    void on_timeout(d20::TimeoutType);

    bool has_outgoing() const;
    std::optional<SeccOutgoing> take_outgoing();

    bool is_finished() const;

    bool is_paused() const {
        return false;
    }

    // A failed negotiation ends the session through the driver-stopped path (immediate close, no
    // EV-first linger), not through the error-termination path, so this stays false.
    bool is_finished_with_error() const {
        return false;
    }

    std::optional<session::feedback::SessionStopAction> pop_session_stop_res_pending() {
        return std::nullopt;
    }

    void request_shutdown();

    // The handshake is paced like ISO 15118-2 / DIN 70121: EVs that cannot cope with an immediate
    // response are most likely to trip over the very first one.
    bool delay_response_after_request() const {
        return true;
    }

    // Return-and-clear the negotiation result. Set exactly once, on a successful negotiation; the
    // Session drains it after the SupportedAppProtocolRes has been written to the socket and replaces
    // this engine with the one for the negotiated protocol. Returns by value because taking it is the
    // last thing that happens before this engine is destroyed.
    std::optional<Negotiated> take_negotiated();

private:
    io::StreamOutputView output_view;
    session::Feedback feedback;

    // The config inputs of the negotiation, copied so the engine does not outlive-reference the
    // Session's config.
    const std::vector<ProtocolId> supported_protocols;
    const std::vector<message_20::datatypes::ServiceCategory> supported_energy_services;
    const bool selecting_sap_based_on_energy_service;
    const std::optional<std::string> custom_protocol;
    const bool tls_active;

    std::optional<SeccOutgoing> outgoing{std::nullopt};
    std::optional<Negotiated> negotiated{std::nullopt};
    // The handshake cannot continue: negotiation failed, an unexpected message arrived or the wait for
    // the request timed out. Reported once any staged (FAILED_*) response has been flushed.
    bool stopped{false};
};

} // namespace iso15118
