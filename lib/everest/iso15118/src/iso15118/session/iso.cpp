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
#include <iso15118/session/sap_engine.hpp>
#include <iso15118/session/secc_engine.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118 {

static constexpr auto SESSION_IDLE_TIMEOUT_MS = 5000;
// After the session ended (SessionStopRes sent), wait this long for the EVCC to close the TCP
// connection first (DIN [V2G-DC-937/938], ISO 15118-20 [V2G20-1633]). Must stay below the -4 ATS
// par_CMN_TCP_Connection_Termination_Timeout of 5 s. Poll-driven: blocking here would stall the
// shared SDP server. Negotiation failures and plug-out bypass it entirely ([V2G-DC-940]).
static constexpr auto CONNECTION_CLOSE_LINGER_MS = 4000;
// The TCP connection is closed immediately on an error end, but DLINK_TERMINATE is held back this
// long: it makes SLAC leave the logical network, and the FIN must traverse the AVLN first or the
// peer never observes the close.
static constexpr auto DLINK_SIGNAL_GRACE_MS = 300;
static constexpr auto MIN_RESPONSE_INTERVAL_MS = 100; // minimum time between two response messages
// ISO 15118-2 / DIN 70121: send each response this long after its request (minus processing time).
// Some EVs' controllers crash if the SECC answers too fast (EvseV2G MAX_RES_TIME parity).
static constexpr auto RESPONSE_DELAY_AFTER_REQUEST_MS = 100;

// DIN [V2G-DC-957], Table 75: 5 s guards the wait for the next CurrentDemandReq, against the
// generic 60 s V2G_SECC_Sequence_Timeout.
static constexpr auto DIN_SEQUENCE_TIMEOUT_CURRENT_DEMAND_MS = 5000;

static uint32_t sequence_timeout_after_response(const V2gMessageType& response_type) {
    if (const auto* din_type = std::get_if<message_din::Type>(&response_type)) {
        if (*din_type == message_din::Type::CurrentDemandRes) {
            return DIN_SEQUENCE_TIMEOUT_CURRENT_DEMAND_MS;
        }
    }
    return d20::TIMEOUT_SEQUENCE;
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

void PowerPath::observe(session::feedback::Signal signal) {
    using Signal = session::feedback::Signal;
    switch (signal) {
    case Signal::CHARGE_LOOP_STARTED:
        charge_loop_running = true;
        break;
    case Signal::CHARGE_LOOP_FINISHED:
        charge_loop_running = false;
        break;
    case Signal::AC_CLOSE_CONTACTOR:
        ac_contactor_closed = true;
        break;
    case Signal::AC_OPEN_CONTACTOR:
        ac_contactor_closed = false;
        break;
    default:
        break;
    }
}

std::vector<session::feedback::Signal> PowerPath::take_teardown_signals() {
    using Signal = session::feedback::Signal;

    std::vector<Signal> signals;
    if (charge_loop_running) {
        signals.push_back(Signal::CHARGE_LOOP_FINISHED);
    }
    if (ac_contactor_closed) {
        signals.push_back(Signal::AC_OPEN_CONTACTOR);
    } else if (charge_loop_running) {
        // A running charge loop with no AC contactor closed is a DC one: the DC contactor closes with
        // PowerDelivery(Start) and has no close signal of its own, only DC_OPEN_CONTACTOR.
        signals.push_back(Signal::DC_OPEN_CONTACTOR);
    }

    charge_loop_running = false;
    ac_contactor_closed = false;
    return signals;
}

Session::Session(std::unique_ptr<io::IConnection> connection_, session::SessionConfig session_config,
                 const session::feedback::Callbacks& callbacks_, std::optional<d20::PauseContext>& pause_ctx_,
                 std::optional<d2::PauseContext>& d2_pause_ctx_) :
    connection(std::move(connection_)),
    config(std::move(session_config)),
    callbacks(callbacks_),
    feedback(callbacks_),
    pause_ctx(pause_ctx_),
    d2_pause_ctx(d2_pause_ctx_),
    engine(std::in_place_type<SapEngine>, engine_output_view(), config, callbacks_, connection->is_secure()) {

    next_session_event = offset_time_point_by_ms(get_current_time_point(), SESSION_IDLE_TIMEOUT_MS);
    connection->set_event_callback([this](io::ConnectionEvent event) { this->handle_connection_event(event); });

    // Latch the power-path signals on their way to the module so an aborted teardown can undo them.
    // The SapEngine and `feedback` keep the plain copy: the handshake closes no contactor, and the
    // Session's own teardown signals must not be re-latched.
    callbacks.signal = [this, forward = callbacks_.signal](session::feedback::Signal signal) {
        power_path.observe(signal);
        if (forward) {
            forward(signal);
        }
    };

    // The engines see only the EXI payload; attach the frame being dispatched so the module can
    // publish header + payload (EvseV2G v2g_messages parity). Responses are emitted from
    // send_response(), which has the frame in hand.
    callbacks.v2g_message = [this, forward = callbacks_.v2g_message](const V2gMessageType& type,
                                                                     const io::StreamInputView&) {
        if (forward) {
            forward(type, current_request_frame);
        }
    };
}

Session::Session(std::unique_ptr<io::IConnection> connection_, session::SessionConfig session_config,
                 const session::feedback::Callbacks& callbacks_, std::optional<d20::PauseContext>& pause_ctx_,
                 std::optional<d2::PauseContext>& d2_pause_ctx_, bool skip_app_protocol_negotiation) :
    Session(std::move(connection_), std::move(session_config), callbacks_, pause_ctx_, d2_pause_ctx_) {
    if (skip_app_protocol_negotiation) {
        // External SAP on a handed-over socket: the handshake already ran, so start directly on the -20
        // engine, which expects a SessionSetupReq first. The vehicle certificate hash arrives with the
        // connection instead of the TLS OPEN event.
        vehicle_cert_hash = connection->get_vehicle_cert_hash();
        engine.emplace<D20SeccEngine>(engine_output_view(), config, pause_ctx, callbacks, timeouts,
                                      d20::EVSupportedAppProtocols{}, message_20::SupportedAppProtocol{},
                                      vehicle_cert_hash, skip_app_protocol_negotiation);
    }
}

Session::~Session() = default;

io::StreamOutputView Session::engine_output_view() {
    return io::StreamOutputView{response_buffer + io::SdpPacket::V2GTP_HEADER_SIZE,
                                sizeof(response_buffer) - io::SdpPacket::V2GTP_HEADER_SIZE};
}

bool Session::is_finished() const {
    // True only once the end-of-session handling completed and the session can be reaped; the logical
    // end of the V2G session is session_over().
    return finished_reported;
}

bool Session::session_over() const {
    // The engines keep is_finished() false while a response is staged, so a session-ending response
    // is always flushed before teardown starts.
    return driver_stopped or visit_engine([](const auto& e) { return e.is_finished(); });
}

session::feedback::Signal Session::teardown_signal() const {
    using Signal = session::feedback::Signal;

    // A paused session keeps the link Matched and only asks for power-saving mode [V2G2-725] /
    // [V2G20-1777]. DIN 70121 has no pause.
    if (visit_engine([](const auto& e) { return e.is_paused(); })) {
        return Signal::DLINK_PAUSE;
    }

    // A positive SessionStopRes(Terminate) releases the link [V2G2-724] / [V2G20-1776] / [V2G-DC-451].
    if (clean_session_end) {
        return Signal::DLINK_TERMINATE;
    }

    // Every other end is an error, and it gets D-LINK_ERROR rather than D-LINK_TERMINATE
    // ([V2G2-727]/[V2G20-727]): per ISO 15118-3 Table 6 that also restarts matching through CP state E,
    // so the EV can retry without being unplugged, where TERMINATE would leave it stuck. DIN defines no
    // such primitive but wants the same outcome ([V2G-DC-942], NOTE at [V2G-DC-943]), so all three
    // generations are treated alike (EvseV2G d_link_action parity).
    return Signal::DLINK_ERROR;
}

void Session::finish_session() {
    if (finished_reported) {
        return;
    }
    finished_reported = true;

    connection->close(); // idempotent; no-op if the EV already closed and the EOF path cleaned up
    open_power_path();

    feedback.signal(teardown_signal());
}

void Session::open_power_path() {
    for (const auto signal : power_path.take_teardown_signals()) {
        feedback.signal(signal);
    }
}

void Session::push_control_event(const d20::ControlEvent& event) {
    control_event_queue.push(event);
}

TimePoint const& Session::poll() {
    const auto now = get_current_time_point();
    // This is the default next session event, which is used when nothing else happens.
    next_session_event = offset_time_point_by_ms(now, SESSION_IDLE_TIMEOUT_MS);

    // Never read while a complete packet is still waiting: the handover below defers a request that
    // arrived before the handshake response could be sent.
    if (state.connected and state.new_data and not packet.is_complete()) {
        switch (read_single_v2gtp_packet(*connection, packet)) {
        case V2GTPReadResult::connection_closed:
            // TCP EOF / TLS close_notify: the EV closed first, which is the regular end (DIN [V2G-DC-937],
            // [V2G2-025]), but also covers a mid-session disconnect. Closing now avoids a sequence timeout.
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
        // Nothing has happened yet, or the connection is gone. If the V2G session is logically over,
        // complete the teardown now so the controller can reap it; this also ends the linger early.
        if (session_over()) {
            finish_session();
        }
        return next_session_event;
    }

    while (const auto event = control_event_queue.try_pop()) {
        // Remember the latest StopCharging request: the handshake engine cannot act on it, so it is
        // re-delivered to the protocol engine once that takes over (see create_engine()).
        if (const auto* stop = std::get_if<d20::StopCharging>(&event.value())) {
            pending_stop_charging = static_cast<bool>(*stop);
        }
        visit_engine([&event](auto& e) { e.on_control_event(event.value()); });
    }

    const auto timeouts_reached = timeouts.check();

    if (timeouts_reached.has_value()) {
        for (const auto& timeout : timeouts_reached.value()) {
            visit_engine([timeout](auto& e) { e.on_timeout(timeout); });
            timeouts.reset_timeout(timeout);
        }
    }

    // check for complete sdp packet
    if (packet.is_complete()) {
        if (session_over()) {
            // The V2G session already ended and we only await the EV's close: drop the data rather than feed
            // a finished engine.
            logf_warning("Ignoring data received after the V2G session ended");
            packet.reset();
            state.new_data = false; // reset new_data flag
        } else if (in_sap_phase() and visit_engine([](const auto& e) { return e.has_outgoing(); })) {
            // The SupportedAppProtocolRes is staged but paced, so the handover has not happened. Keep the
            // packet for the engine that will own the protocol instead of failing it against the handshake
            // engine; the response is due within RESPONSE_DELAY_AFTER_REQUEST_MS, so this defers by one poll.
        } else {
            const auto payload_type = packet.get_payload_type();
            const io::StreamInputView view{packet.get_payload_buffer(), packet.get_payload_length()};

            // Timestamp the request so -2/DIN and the handshake can pace their response after it.
            last_request_rx_time = now;

            if (not in_sap_phase()) {
                // The first request to a protocol engine is the SessionSetupReq: the session is established, so
                // V2G_SECC_CommunicationSetup_Timeout stops and the per-message sequence timeout takes over.
                v2g_session_established = true;
                // A sequence timer is armed on every response; stop it as soon as the next request arrives.
                timeouts.stop_timeout(d20::TimeoutType::SEQUENCE);
            }

            // Publish the frame the engine is about to decode; cleared right after, the buffer is reused.
            current_request_frame = {packet.get_buffer(),
                                     packet.get_payload_length() + io::SdpPacket::V2GTP_HEADER_SIZE};
            visit_engine([payload_type, &view](auto& e) { e.on_packet(payload_type, view); });
            current_request_frame = {};

            packet.reset();
            state.new_data = false; // reset new_data flag
        }
    }

    // Send a pending response, but not before it is due: -2/DIN and the handshake pace it
    // RESPONSE_DELAY_AFTER_REQUEST_MS after the request, ISO 15118-20 only keeps responses
    // MIN_RESPONSE_INTERVAL_MS apart. Overrunning the window sends immediately and warns.
    const bool has_outgoing = visit_engine([](const auto& e) { return e.has_outgoing(); });
    if (has_outgoing) {
        if (not response_send_after.has_value()) {
            const bool pace_after_request =
                visit_engine([](const auto& e) { return e.delay_response_after_request(); });
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

        if (response_send_after.has_value() && now < response_send_after.value()) {
            next_session_event = response_send_after.value();
        } else {
            response_send_after.reset();
            send_response();
        }
    } else {
        response_send_after.reset();
    }

    // Runs after the send above: the handover must not swap the engine out from under a staged response.
    advance_sap_handover();

    if (session_over() and not finished_reported) {
        if (error_termination or visit_engine([](const auto& e) { return e.is_finished_with_error(); })) {
            // FIN out now ([V2G-DC-940]) but hold the D-LINK signal for DLINK_SIGNAL_GRACE_MS.
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
            // Error / abnormal termination. [V2G-DC-940]: terminate WITHOUT delay -- no EV-first linger, which
            // is only for a normal end. The -4 ATS checks this (TC_SECC_VTB_SupportedAppProtocol_002) against
            // par_CMN_TCP_Connection_Termination_Timeout and par_SECC_CPOscillator_Shutdown_Timeout. Any
            // pending response was flushed earlier this poll() and TCP delivers queued bytes before the FIN,
            // so the FAILED response still reaches the EV.
            finish_session();
        } else {
            // Normal end but the EV is still connected: give it CONNECTION_CLOSE_LINGER_MS to close first;
            // its close lands as EOF and finishes via the not-connected path. Never block -- the poll loop is
            // shared with the SDP server.
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

void Session::advance_sap_handover() {
    auto* sap = std::get_if<SapEngine>(&engine);
    if (sap == nullptr) {
        return; // already running on a protocol engine
    }

    if (sap->has_outgoing()) {
        // The paced SupportedAppProtocolRes is not on the wire yet, and replacing the variant alternative
        // would destroy the engine that staged it. Come back once it is sent.
        return;
    }

    if (auto negotiated = sap->take_negotiated()) {
        // Taken by value: create_engine() destroys the SapEngine holding the original.
        if (not create_engine(*negotiated)) {
            logf_error("No engine available for the negotiated protocol, terminating session");
            driver_stopped = true;
        } else if (packet.is_complete()) {
            // A request deferred while the handshake response was pending is now waiting for the new engine;
            // nothing else would wake the driver, so poll again immediately.
            next_session_event = get_current_time_point();
        }
        return;
    }

    if (sap->is_finished()) {
        // Negotiation failed or timed out; any FAILED_* was flushed above, so tear down without the
        // EV-first linger ([V2G-DC-940]).
        driver_stopped = true;
    }
}

bool Session::create_engine(const SapEngine::Negotiated& negotiated) {
    const auto created = [&]() {
        switch (negotiated.protocol_id) {
        case ProtocolId::ISO15118_20:
            engine.emplace<D20SeccEngine>(engine_output_view(), config, pause_ctx, callbacks, timeouts,
                                          negotiated.offered_protocols, negotiated.selected_protocol,
                                          vehicle_cert_hash);
            return true;
        case ProtocolId::ISO15118_2:
            engine.emplace<D2SeccEngine>(engine_output_view(), config, d2_pause_ctx, callbacks, timeouts,
                                         connection->is_secure());
            return true;
        case ProtocolId::DIN70121:
            engine.emplace<DinSeccEngine>(engine_output_view(), config, callbacks, timeouts);
            return true;
        }
        return false;
    }();

    // A stop requested during the handshake reached only the SapEngine, which ignores control events:
    // hand it to the fresh protocol engine so the EV is told to stop from the first response on.
    if (created and pending_stop_charging) {
        const d20::ControlEvent stop_event{d20::StopCharging{true}};
        visit_engine([&stop_event](auto& e) { e.on_control_event(stop_event); });
    }
    return created;
}

void Session::send_response() {
    const auto outgoing = visit_engine([](auto& e) { return e.take_outgoing(); });
    if (not outgoing.has_value()) {
        return;
    }

    const auto response_type = outgoing->message_type;
    const auto response_size = setup_response_header(response_buffer, outgoing->payload_type, outgoing->payload_size);
    connection->write(response_buffer, response_size);
    last_response_tx_time = get_current_time_point();

    timeouts.start_timeout(d20::TimeoutType::SEQUENCE, sequence_timeout_after_response(response_type));

    feedback.v2g_message(response_type, {response_buffer, response_size});

    // A session-ending response hit the wire. For a positive SessionStopRes this anchors the
    // CP-oscillator retain time [V2G-DC-968]; only that timing hangs off this feedback, so the close
    // linger and the DLINK_* signals keep their own anchors. A FAILED_* end skips the linger.
    if (const auto stop_action = visit_engine([](auto& e) { return e.pop_session_stop_res_pending(); })) {
        if (*stop_action == session::feedback::SessionStopAction::FailedTermination) {
            error_termination = true;
        } else {
            // One of the two regular ends, so release the link with D-LINK_TERMINATE / D-LINK_PAUSE.
            clean_session_end = true;
        }
        feedback.session_stop_res_sent(*stop_action);
    }
}

void Session::handle_connection_event(io::ConnectionEvent event) {
    using Event = io::ConnectionEvent;
    switch (event) {
    case Event::ACCEPTED:
        assert(state.connected == false);
        state.connected = true;
        logf_info("Accepted connection on port %d", connection->get_public_endpoint().port);
        // Guard the wait for the first request with the sequence timeout, so an EV that connects and
        // sends nothing is closed rather than left open (EvseV2G parity).
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
        // The transport is gone: mark the driver stopped so the controller reaps the session. A lingering
        // session otherwise blocks the SDP server until the 40 s sequence timeout fires.
        driver_stopped = true;
        return;
    }
}

void Session::close() {
    // Immediate close, bypassing the linger: D-LINK loss (plug-out), kill and shutdown
    // ([V2G-DC-940]). Always DLINK_TERMINATE -- a killed session is not a pause, and not an error the
    // SECC identified either, so [V2G2-726] has it simply go back to waiting rather than run the
    // D-LINK_ERROR recovery at a connector with no EV on it.
    driver_stopped = true;
    if (finished_reported) {
        return; // already closed and signaled
    }
    finished_reported = true;
    connection->close();
    open_power_path();
    feedback.signal(session::feedback::Signal::DLINK_TERMINATE);
}

void Session::request_shutdown() {
    if (not state.connected) {
        logf_info("Shutdown requested before an EV connected");
        close();
    } else {
        push_control_event(d20::StopCharging{true}); // Stopping active charge loop
        visit_engine([](auto& e) { e.request_shutdown(); });
    }
}

} // namespace iso15118
