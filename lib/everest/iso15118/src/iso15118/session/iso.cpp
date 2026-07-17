// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/iso.hpp>

#include <cassert>
#include <chrono>
#include <cstring>
#include <string>

#include <arpa/inet.h>

#include <iso15118/session/d20_secc_engine.hpp>
#include <iso15118/session/d2_secc_engine.hpp>
#include <iso15118/session/din_secc_engine.hpp>
#include <iso15118/session/secc_engine.hpp>
#include <iso15118/session/secc_sap.hpp>

#include <iso15118/message/variant.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118 {

static constexpr auto SESSION_IDLE_TIMEOUT_MS = 5000;
// After the session ended (SessionStopRes sent), wait this long for the EVCC to close the TCP
// connection first before we close it ourselves (DIN [V2G-DC-937/938], ISO 15118-20 [V2G20-1633]).
// It bounds the SECC self-close for the cases that reach it: a graceful SessionStop (the EV normally
// closes first, so this rarely fires) and engine-level FAILED/timeout terminations
// (FAILED_SequenceError / FAILED_UnknownSession / V2G_Sequence_Timeout) which end via
// engine->is_finished(). It MUST stay below par_CMN_TCP_Connection_Termination_Timeout -- the -4 ATS
// upper bound of 5 s within which the SUT must close after a failure/timeout (asserted as
// latency <= 5 s, an upper bound with no min-wait); 4 s leaves a 1 s margin while still giving a
// compliant EV ample time to close first (it typically closes in < 1 s). SupportedAppProtocol
// negotiation failures and plug-out/kill bypass this linger entirely and close immediately (the
// driver_stopped branch in poll() and Session::close(); [V2G-DC-940] terminate without delay). Real
// EVs commonly dislike an SECC-initiated close, so an EV-first close is still preferred. The wait is
// poll-driven -- it must never block the controller loop (a blocked loop stalls the SDP server and
// delays the D-LINK signal into the next plug-in cycle).
static constexpr auto CONNECTION_CLOSE_LINGER_MS = 4000;
// On an SECC-initiated error close ([V2G-DC-940]: FAILED end, CP State A) the TCP connection is
// closed immediately (FIN out), but the DLINK_TERMINATE signal is held back this long: it makes
// SLAC leave the logical network, and the FIN must traverse the AVLN first, otherwise the peer can
// never observe the close (the SLAC leave itself has T_match_leave of budget, so a short grace is
// standards-safe). An EV-first close (EOF) still finishes immediately via the not-connected path.
static constexpr auto DLINK_SIGNAL_GRACE_MS = 300;
static constexpr auto MIN_RESPONSE_INTERVAL_MS = 100; // minimum time between two response messages
// ISO 15118-2 / DIN 70121: send each response this long after its request was received (deducting the
// internal processing time). Some EVs are not ready to receive the response immediately and their
// controller may crash if the SECC answers too fast (mirrors EvseV2G's MAX_RES_TIME behaviour).
static constexpr auto RESPONSE_DELAY_AFTER_REQUEST_MS = 100;

// DIN SPEC 70121 [V2G-DC-957], Table 75: after a CurrentDemandRes the SECC guards the wait for the next
// CurrentDemandReq with V2G_SECC_Sequence_TimeoutCR = 5 s, tighter than the generic 60 s
// V2G_SECC_Sequence_Timeout used for every other message.
static constexpr auto DIN_SEQUENCE_TIMEOUT_CURRENT_DEMAND_MS = 5000;

// Picks the SECC sequence timeout to arm after a response has been sent. Only DIN CurrentDemandRes
// deviates from the generic 60 s value.
static uint32_t sequence_timeout_after_response(const V2gMessageType& response_type) {
    if (const auto* din_type = std::get_if<message_din::Type>(&response_type)) {
        if (*din_type == message_din::Type::CurrentDemandRes) {
            return DIN_SEQUENCE_TIMEOUT_CURRENT_DEMAND_MS;
        }
    }
    return d20::TIMEOUT_SEQUENCE;
}

static void log_packet_from_car(const iso15118::io::SdpPacket& packet, session::SessionLogger& logger) {
    logger.exi(static_cast<uint16_t>(packet.get_payload_type()), packet.get_payload_buffer(),
               packet.get_payload_length(), session::logging::ExiMessageDirection::FROM_EV);
}

void raise_invalid_packet_state(const io::SdpPacket& sdp_packet) {
    using PacketState = io::SdpPacket::State;

    auto error = std::string("Error while reading sdp packet: ");
    switch (sdp_packet.get_state()) {
    case PacketState::INVALID_HEADER:
        error += "invalid sdp packet header";
        break;
    case PacketState::PAYLOAD_TOO_LONG:
        error += "packet too large for buffer";
        break;
    default:
        assert(false);
    }

    log_and_throw(error.c_str());
}

namespace {
enum class V2GTPReadResult {
    complete,          //!< a full packet was read
    would_block,       //!< more data is needed to complete the packet
    connection_closed, //!< the peer closed the connection mid-read
};
} // namespace

// NOTE (aw): this function reports a tri-state result:
//            - would_block: it would block to read a complete packet
//            - complete: the packet is complete
//            - connection_closed: the peer closed the connection during the read
V2GTPReadResult read_single_v2gtp_packet(io::IConnection& connection, io::SdpPacket& sdp_packet) {
    // NOTE (aw): not happy with this function
    //            main problem is, that it combines too much logic of the sdp packet and io related stuff
    using PacketState = io::SdpPacket::State;

    assert(sdp_packet.get_state() == PacketState::BUFFER_EMPTY || sdp_packet.get_state() == PacketState::HEADER_READ);

    const auto first_try =
        connection.read(sdp_packet.get_current_buffer_pos(), sdp_packet.get_remaining_bytes_to_read());

    if (first_try.connection_closed) {
        return V2GTPReadResult::connection_closed;
    }

    sdp_packet.update_read_bytes(first_try.bytes_read);

    if (first_try.would_block) {
        // need more data for at least the header
        return V2GTPReadResult::would_block;
    }

    if (sdp_packet.get_state() == PacketState::COMPLETE) {
        // done
        return V2GTPReadResult::complete;
    }

    // packet not finished
    if (sdp_packet.get_state() != PacketState::HEADER_READ) {
        raise_invalid_packet_state(sdp_packet);
    }

    // header read successfully, try to read the rest
    const auto second_try =
        connection.read(sdp_packet.get_current_buffer_pos(), sdp_packet.get_remaining_bytes_to_read());

    if (second_try.connection_closed) {
        return V2GTPReadResult::connection_closed;
    }

    sdp_packet.update_read_bytes(second_try.bytes_read);

    if (second_try.would_block) {
        // need more data for the rest of the packet!
        return V2GTPReadResult::would_block;
    }

    // assert finished packet
    if (sdp_packet.get_state() != PacketState::COMPLETE) {
        raise_invalid_packet_state(sdp_packet);
    }

    return V2GTPReadResult::complete;
}

static size_t setup_response_header(uint8_t* buffer, iso15118::io::v2gtp::PayloadType payload_type, size_t size) {
    buffer[0] = iso15118::io::SDP_PROTOCOL_VERSION;
    buffer[1] = iso15118::io::SDP_INVERSE_PROTOCOL_VERSION;

    const uint16_t response_payload_type =
        htons(static_cast<std::underlying_type_t<iso15118::io::v2gtp::PayloadType>>(payload_type));

    std::memcpy(buffer + 2, &response_payload_type, sizeof(response_payload_type));

    const uint32_t tmp32 = htonl(size);

    std::memcpy(buffer + 4, &tmp32, sizeof(tmp32));

    return size + iso15118::io::SdpPacket::V2GTP_HEADER_SIZE;
}

Session::Session(std::unique_ptr<io::IConnection> connection_, session::SessionConfig session_config,
                 const session::feedback::Callbacks& callbacks_, std::optional<d20::PauseContext>& pause_ctx_,
                 std::optional<d2::PauseContext>& d2_pause_ctx_) :
    connection(std::move(connection_)),
    log(this),
    config(std::move(session_config)),
    callbacks(callbacks_),
    feedback(callbacks_),
    pause_ctx(pause_ctx_),
    d2_pause_ctx(d2_pause_ctx_) {

    next_session_event = offset_time_point_by_ms(get_current_time_point(), SESSION_IDLE_TIMEOUT_MS);
    connection->set_event_callback([this](io::ConnectionEvent event) { this->handle_connection_event(event); });
}

Session::~Session() = default;

bool Session::is_finished() const {
    // Controller-facing: true only once the end-of-session handling (waiting for an EV-initiated TCP
    // close, closing the connection, sending the D-LINK signal) has completed and the session can be
    // reaped. The logical end of the V2G session itself is session_over().
    return finished_reported;
}

bool Session::session_over() const {
    if (pending_sap_outgoing.has_value()) {
        return false;
    }
    if (engine) {
        return driver_stopped or engine->is_finished();
    }
    return driver_stopped;
}

void Session::finish_session() {
    if (finished_reported) {
        return;
    }
    finished_reported = true;

    connection->close(); // idempotent; no-op if the EV already closed and the EOF path cleaned up

    const bool paused = engine and engine->is_paused();
    const auto signal = paused ? session::feedback::Signal::DLINK_PAUSE : session::feedback::Signal::DLINK_TERMINATE;
    feedback.signal(signal);
}

void Session::push_control_event(const d20::ControlEvent& event) {
    control_event_queue.push(event);
}

TimePoint const& Session::poll() {
    const auto now = get_current_time_point();
    // This is the default next session event, which is used when nothing else happens.
    next_session_event = offset_time_point_by_ms(now, SESSION_IDLE_TIMEOUT_MS);

    // check for new data to read
    if (state.connected and state.new_data) {
        switch (read_single_v2gtp_packet(*connection, packet)) {
        case V2GTPReadResult::connection_closed:
            // TCP EOF / TLS close_notify: the peer (EV) closed the connection. This is the regular
            // way a session ends -- the EVCC closes first (DIN [V2G-DC-937], ISO 15118-2 [V2G2-025])
            // -- but it also covers a mid-session EV disconnect. Close our side now (idempotent);
            // the not-connected path below completes the end-of-session handling if the V2G session
            // is logically over, instead of running into a sequence timeout.
            logf_info("TCP connection closed by the peer");
            connection->close();
            break;
        case V2GTPReadResult::would_block:
            state.new_data = false;
            break;
        case V2GTPReadResult::complete:
            break;
        }
    }

    if (not state.connected) {
        // Either nothing happened so far (still waiting for the TCP accept), or the connection is
        // gone (EV-initiated close or a local close). If the V2G session is logically over, complete
        // the end-of-session handling now so the controller can reap the session; an EV closing
        // right after SessionStopRes ends the linger early (DIN [V2G-DC-937]: EV-first close is the
        // regular case).
        if (session_over()) {
            finish_session();
        }
        return next_session_event;
    }

    // deliver queued control events to the engine (dropped during the handshake, which ignores them)
    while (const auto event = control_event_queue.pop()) {
        if (engine) {
            engine->on_control_event(event.value());
        }
    }

    const auto timeouts_reached = timeouts.check();

    if (timeouts_reached.has_value()) {
        for (const auto& timeout : timeouts_reached.value()) {
            if (engine) {
                engine->on_timeout(timeout);
            } else {
                logf_error("Timeout reached during SupportedAppProtocol handshake. Stopping the session");
                driver_stopped = true;
            }
            timeouts.reset_timeout(timeout);
        }
    }

    // check for complete sdp packet
    if (packet.is_complete()) {
        if (session_over()) {
            // The V2G session already ended (we are only waiting for the EV to close the TCP
            // connection); no request is allowed anymore -- drop the data instead of feeding it to a
            // finished engine.
            logf_warning("Ignoring data received after the V2G session ended");
            packet = {};
        } else {
            log_packet_from_car(packet, log);

            const auto payload_type = packet.get_payload_type();
            const io::StreamInputView view{packet.get_payload_buffer(), packet.get_payload_length()};

            // Timestamp the request so -2/DIN (and the SupportedAppProtocol handshake) can pace their
            // response a fixed delay after it (see below).
            last_request_rx_time = now;

            if (engine) {
                // The first request handed to the engine is the SessionSetupReq: the V2G communication
                // session is now established, so the controller stops V2G_SECC_CommunicationSetup_Timeout
                // (from here the per-message V2G_SECC_Sequence_Timeout governs).
                v2g_session_established = true;
                // A sequence timer is armed on every response we send; stop it as soon as the next request
                // arrives (there is no sequence timer during the SupportedAppProtocol handshake).
                timeouts.stop_timeout(d20::TimeoutType::SEQUENCE);
                engine->on_packet(payload_type, view);
            } else {
                handle_supported_app_protocol_request(payload_type, view);
            }

            packet = {}; // reset the packet
        }
    }

    // Send a pending response, but not before it is due.
    //
    // ISO 15118-2 / DIN 70121 (delay_response_after_request()) and the shared SupportedAppProtocol
    // handshake: send the response RESPONSE_DELAY_AFTER_REQUEST_MS after the request was received,
    // deducting the time already spent processing it. If processing took longer than that window, send
    // immediately and warn that the timing could not be met. This protects EVs whose controller crashes
    // if the SECC answers too fast.
    //
    // Otherwise (ISO 15118-20): pace responses at least MIN_RESPONSE_INTERVAL_MS apart to avoid
    // potential performance issues.
    const bool has_outgoing = pending_sap_outgoing.has_value() or (engine and engine->has_outgoing());
    if (has_outgoing) {
        if (not response_send_after.has_value()) {
            // Note: handle_supported_app_protocol_request() creates the engine inline, so `engine` is
            // already non-null while the SAP response is pending; key the SAP case off pending_sap_outgoing.
            const bool pace_after_request =
                pending_sap_outgoing.has_value() or (engine and engine->delay_response_after_request());
            if (pace_after_request and last_request_rx_time.has_value()) {
                const auto due = offset_time_point_by_ms(last_request_rx_time.value(), RESPONSE_DELAY_AFTER_REQUEST_MS);
                if (now < due) {
                    response_send_after = due;
                } else {
                    const auto took =
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_request_rx_time.value());
                    logf_warning("Response not ready within %d ms after the request (took %lld ms), sending "
                                 "immediately",
                                 RESPONSE_DELAY_AFTER_REQUEST_MS, static_cast<long long>(took.count()));
                    response_send_after = now;
                }
            } else {
                response_send_after = now;
                if (last_response_tx_time.has_value()) {
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        get_current_time_point() - last_response_tx_time.value());
                    if (elapsed < std::chrono::milliseconds(MIN_RESPONSE_INTERVAL_MS)) {
                        response_send_after =
                            offset_time_point_by_ms(last_response_tx_time.value(), MIN_RESPONSE_INTERVAL_MS);
                    }
                }
            }
        }

        // Send the response as soon as it is due
        if (response_send_after.has_value() && now < response_send_after.value()) {
            next_session_event = response_send_after.value();
        } else {
            response_send_after.reset();
            send_response();
        }
    } else {
        response_send_after.reset();
    }

    if (session_over() and not finished_reported) {
        if (error_termination or (engine and engine->is_finished_with_error())) {
            // SECC-initiated error close: FIN out NOW ([V2G-DC-940]), but hold the DLINK_TERMINATE
            // back for DLINK_SIGNAL_GRACE_MS so the FIN traverses the AVLN before SLAC leaves it.
            connection->close(); // idempotent
            if (not dlink_signal_deadline.has_value()) {
                dlink_signal_deadline = offset_time_point_by_ms(now, DLINK_SIGNAL_GRACE_MS);
            }
            if (now >= dlink_signal_deadline.value()) {
                finish_session();
            } else {
                next_session_event = std::min(next_session_event, dlink_signal_deadline.value());
            }
        } else if (driver_stopped) {
            // Error / abnormal termination: the SupportedAppProtocol negotiation failed
            // (FAILED_NoNegotiation), an unexpected message arrived, the handshake timed out, the
            // negotiated namespace was unknown, or the engine could not be created. [V2G-DC-940]: the SECC
            // terminates the session WITHOUT delay -- do not wait CONNECTION_CLOSE_LINGER_MS for the EV to
            // close first (that grace is only for a normal successful end, DIN [V2G-DC-937/938]). The -4 ATS
            // (e.g. TC_SECC_VTB_SupportedAppProtocol_002) checks the TCP close and CP oscillator shutdown
            // happen within par_CMN_TCP_Connection_Termination_Timeout (5 s) /
            // par_SECC_CPOscillator_Shutdown_Timeout. Any pending response was already flushed by
            // send_response() earlier this poll(), and TCP delivers the queued bytes before the FIN, so the
            // FAILED response still reaches the EV. finish_session() closes the socket and emits
            // DLINK_TERMINATE (-> Charger::cp_state_X1(), CP oscillator off).
            finish_session();
        } else {
            // Normal successful end (SessionStopRes sent) but the EV is still connected: give it
            // CONNECTION_CLOSE_LINGER_MS to close the TCP connection first (DIN [V2G-DC-937/938],
            // ISO 15118-20 [V2G20-1633]); an EV-initiated close lands as EOF -> CLOSED and finishes via
            // the not-connected path above. A plug-out bypasses the linger entirely: D-LINK down ->
            // TbdController kill -> Session::close() ([V2G-DC-940]: on error/HLE request terminate
            // without delay). Never block here -- the poll loop is shared with the SDP server.
            if (not connection_close_deadline.has_value()) {
                connection_close_deadline = offset_time_point_by_ms(now, CONNECTION_CLOSE_LINGER_MS);
            }
            if (now >= connection_close_deadline.value()) {
                logf_info(
                    "The EV did not close the TCP connection within %d ms after the session ended; closing it now",
                    CONNECTION_CLOSE_LINGER_MS);
                finish_session();
            } else {
                next_session_event = std::min(next_session_event, connection_close_deadline.value());
            }
        }
    }

    return next_session_event;
}

void Session::handle_supported_app_protocol_request(io::v2gtp::PayloadType payload_type,
                                                    const io::StreamInputView& view) {
    const message_20::Variant variant{payload_type, view};
    feedback.v2g_message(variant.get_type());

    const auto req = variant.get_if<message_20::SupportedAppProtocolRequest>();
    if (req == nullptr) {
        log("expected SupportedAppProtocolReq! But code type id: %d", variant.get_type());
        driver_stopped = true;
        return;
    }

    if (config.selecting_sap_based_on_energy_service) {
        logf_info("Selecting supported app protocol namespace based on the supported energy services");
    }

    const auto result = session::secc_sap::handle_request(
        *req, config.supported_protocols, config.supported_energy_transfer_services,
        config.selecting_sap_based_on_energy_service, config.custom_protocol, connection->is_secure());

    const io::StreamOutputView response_view{response_buffer + io::SdpPacket::V2GTP_HEADER_SIZE,
                                             sizeof(response_buffer) - io::SdpPacket::V2GTP_HEADER_SIZE};
    const auto payload_size = message_20::serialize(result.response, response_view);
    pending_sap_outgoing =
        PendingOutgoing{payload_size, io::v2gtp::PayloadType::SAP, message_20::Type::SupportedAppProtocolRes};

    sap_offered_protocols = req->app_protocol;

    if (result.response.response_code == message_20::SupportedAppProtocolResponse::ResponseCode::Failed_NoNegotiation) {
        std::string ev_supported_namespaces{};
        for (const auto& protocol : req->app_protocol) {
            ev_supported_namespaces.append(protocol.protocol_namespace + ";");
        }
        logf_error("Selecting a protocol namespace failed. Ev offered: %s", ev_supported_namespaces.c_str());
        driver_stopped = true;
        return;
    }

    for (const auto& protocol : req->app_protocol) {
        if (result.response.schema_id.has_value() and protocol.schema_id == result.response.schema_id.value()) {
            sap_selected_protocol = protocol;
        }
    }

    const auto& selected_namespace = result.selected_namespace.value();
    if (selected_namespace == ISO20_DC_PROTOCOL_NAMESPACE) {
        feedback.selected_protocol("ISO15118-20:DC");
    } else if (selected_namespace == DIN70121_NAMESPACE) {
        feedback.selected_protocol("DIN70121");
    } else if (selected_namespace == ISO2_NAMESPACE) {
        // EvseV2G-compatible string so downstream consumers (EvseManager / RpcApi) classify this as
        // ISO 15118-2 exactly as the legacy stack does.
        feedback.selected_protocol("ISO15118-2-2013");
    } else if (config.custom_protocol.has_value() and config.custom_protocol.value() == selected_namespace) {
        feedback.selected_protocol(config.custom_protocol.value());
        logf_warning("EV and EVSE have agreed on a custom protocol namespace. Problems or aborts can occur in the "
                     "following states!");
    } else {
        feedback.selected_protocol("ISO15118-20:AC and similar");
    }

    const auto protocol_id =
        session::secc_sap::protocol_id_from_selected_namespace(selected_namespace, config.custom_protocol);
    if (not protocol_id.has_value()) {
        log("SupportedAppProtocol negotiated an unknown namespace, terminating session");
        driver_stopped = true;
        return;
    }

    engine = create_engine(protocol_id.value());
    if (not engine) {
        driver_stopped = true;
        return;
    }
}

std::unique_ptr<SeccEngine> Session::create_engine(ProtocolId protocol_id) {
    switch (protocol_id) {
    case ProtocolId::ISO15118_20: {
        const io::StreamOutputView view{response_buffer + io::SdpPacket::V2GTP_HEADER_SIZE,
                                        sizeof(response_buffer) - io::SdpPacket::V2GTP_HEADER_SIZE};
        return std::make_unique<D20SeccEngine>(view, config, pause_ctx, callbacks, log, timeouts, sap_offered_protocols,
                                               sap_selected_protocol, vehicle_cert_hash);
    }
    case ProtocolId::ISO15118_2: {
        const io::StreamOutputView view{response_buffer + io::SdpPacket::V2GTP_HEADER_SIZE,
                                        sizeof(response_buffer) - io::SdpPacket::V2GTP_HEADER_SIZE};
        return std::make_unique<D2SeccEngine>(view, config, d2_pause_ctx, callbacks, log, timeouts,
                                              connection->is_secure());
    }
    case ProtocolId::DIN70121: {
        const io::StreamOutputView view{response_buffer + io::SdpPacket::V2GTP_HEADER_SIZE,
                                        sizeof(response_buffer) - io::SdpPacket::V2GTP_HEADER_SIZE};
        return std::make_unique<DinSeccEngine>(view, config, callbacks, log, timeouts);
    }
    }
    return nullptr;
}

void Session::send_response() {
    size_t payload_size;
    io::v2gtp::PayloadType payload_type;
    V2gMessageType response_type;

    if (pending_sap_outgoing.has_value()) {
        payload_size = pending_sap_outgoing->payload_size;
        payload_type = pending_sap_outgoing->payload_type;
        response_type = pending_sap_outgoing->message_type;
        pending_sap_outgoing.reset();
    } else if (engine) {
        const auto outgoing = engine->take_outgoing();
        if (not outgoing.has_value()) {
            return;
        }
        payload_size = outgoing->payload_size;
        payload_type = outgoing->payload_type;
        response_type = outgoing->message_type;
    } else {
        return;
    }

    const auto response_size = setup_response_header(response_buffer, payload_type, payload_size);
    connection->write(response_buffer, response_size);
    last_response_tx_time = get_current_time_point();

    timeouts.start_timeout(d20::TimeoutType::SEQUENCE, sequence_timeout_after_response(response_type));

    // FIXME (aw): this is hacky ...
    log.exi(static_cast<uint16_t>(payload_type), response_buffer + io::SdpPacket::V2GTP_HEADER_SIZE, payload_size,
            session::logging::ExiMessageDirection::TO_EV);

    feedback.v2g_message(response_type);

    // A session-ending response just hit the wire (any protocol): report it. For a positive
    // SessionStopRes this is the anchor of the CP-oscillator retain time [V2G-DC-968]; only the
    // oscillator timing hangs off this feedback -- the connection-close linger and the DLINK_*
    // signals keep their own anchors so the EV's TCP close can still complete over the intact link.
    // A FAILED_* end (FailedTermination) additionally skips the EV-first close linger: the SECC
    // closes the TCP connection itself without delay ([V2G-DC-940]).
    if (engine) {
        if (const auto stop_action = engine->pop_session_stop_res_pending()) {
            if (*stop_action == session::feedback::SessionStopAction::FailedTermination) {
                error_termination = true;
            }
            feedback.session_stop_res_sent(*stop_action);
        }
    }
}

void Session::handle_connection_event(io::ConnectionEvent event) {
    using Event = io::ConnectionEvent;
    switch (event) {
    case Event::ACCEPTED:
        assert(state.connected == false);
        state.connected = true;
        log("Accepted connection on port %d", connection->get_public_endpoint().port);
        // Guard the wait for the first (supportedAppProtocol) request with the
        // V2G sequence timeout: if the EV connects but sends nothing, the timer
        // fires and the session driver closes the connection (on par with the
        // EvseV2G stack). Subsequent requests re-arm it via send_response(); it
        // is stopped when the next request arrives (see poll()).
        timeouts.start_timeout(d20::TimeoutType::SEQUENCE, d20::TIMEOUT_SEQUENCE);
        return;

    case Event::NEW_DATA:
        assert(state.connected);
        state.new_data = true;
        return;

    case Event::OPEN:
        assert(state.connected);
        if (const auto new_vehicle_cert_hash = connection->get_vehicle_cert_hash()) {
            logf_info("Vehicle Cert is available");
            vehicle_cert_hash = new_vehicle_cert_hash;
        }
        // NOTE (aw): for now, we don't really need this information ...
        return;

    case Event::CLOSED:
        state.connected = false;
        logf_info("Connection is closed");
        // The transport is gone, so this session cannot continue: mark the
        // driver stopped so the controller reaps it. A lingering session
        // otherwise blocks the SDP server (new discovery attempts are
        // refused) until the 40 s sequence timeout finally fires.
        driver_stopped = true;
        return;
    }
}

void Session::close() {
    // Immediate close, bypassing the end-of-session linger: used on D-LINK loss (plug-out), session
    // kill and internal errors ([V2G-DC-940]: terminate without delay). Always signals
    // DLINK_TERMINATE -- a killed session is never a pause.
    driver_stopped = true;
    if (finished_reported) {
        return; // already closed and signaled
    }
    finished_reported = true;
    connection->close();
    feedback.signal(session::feedback::Signal::DLINK_TERMINATE);
}

void Session::request_shutdown() {
    if (not state.connected) {
        logf_info("Shutdown requested before an EV connected");
        close();
    } else {
        push_control_event(d20::StopCharging{true}); // Stopping active charge loop
        if (engine) {
            engine->request_shutdown();
        }
    }
}

} // namespace iso15118
