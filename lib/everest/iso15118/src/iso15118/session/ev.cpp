// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/ev.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>

#include <arpa/inet.h>

#include <iso15118/d20/ev/timeouts.hpp>
#include <iso15118/session/d20_ev_engine.hpp>
#include <iso15118/session/d2_ev_engine.hpp>
#include <iso15118/session/din_ev_engine.hpp>
#include <iso15118/session/ev_engine.hpp>

#include <iso15118/detail/d20/ev/state/supported_app_protocol.hpp>
#include <iso15118/detail/helper.hpp>

#include <iso15118/message/type.hpp>
#include <iso15118/message/variant.hpp>

namespace iso15118 {

static constexpr auto SESSION_IDLE_TIMEOUT_MS = 5000;

namespace {

namespace dt = message_20::datatypes;

void log_packet_from_secc(const io::SdpPacket& packet, session::SessionLogger& logger) {
    logger.exi(static_cast<uint16_t>(packet.get_payload_type()), packet.get_payload_buffer(),
               packet.get_payload_length(), session::logging::ExiMessageDirection::TO_EV);
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

// mirror of the SECC-side read_single_sdp_packet (iso.cpp); returns true if it would block.
bool read_packet(io::IConnection& connection, io::SdpPacket& sdp_packet) {
    using PacketState = io::SdpPacket::State;

    assert(sdp_packet.get_state() == PacketState::BUFFER_EMPTY || sdp_packet.get_state() == PacketState::HEADER_READ);

    const auto first_try =
        connection.read(sdp_packet.get_current_buffer_pos(), sdp_packet.get_remaining_bytes_to_read());
    sdp_packet.update_read_bytes(first_try.bytes_read);

    if (first_try.would_block) {
        return true;
    }

    if (sdp_packet.get_state() == PacketState::COMPLETE) {
        return false;
    }

    if (sdp_packet.get_state() != PacketState::HEADER_READ) {
        raise_invalid_packet_state(sdp_packet);
    }

    const auto second_try =
        connection.read(sdp_packet.get_current_buffer_pos(), sdp_packet.get_remaining_bytes_to_read());
    sdp_packet.update_read_bytes(second_try.bytes_read);

    if (second_try.would_block) {
        return true;
    }

    if (sdp_packet.get_state() != PacketState::COMPLETE) {
        raise_invalid_packet_state(sdp_packet);
    }

    return false;
}

size_t setup_request_header(uint8_t* buffer, io::v2gtp::PayloadType payload_type, size_t size) {
    buffer[0] = io::SDP_PROTOCOL_VERSION;
    buffer[1] = io::SDP_INVERSE_PROTOCOL_VERSION;

    const uint16_t request_payload_type =
        htons(static_cast<std::underlying_type_t<io::v2gtp::PayloadType>>(payload_type));
    std::memcpy(buffer + 2, &request_payload_type, sizeof(request_payload_type));

    const uint32_t tmp32 = htonl(size);
    std::memcpy(buffer + 4, &tmp32, sizeof(tmp32));

    return size + io::SdpPacket::V2GTP_HEADER_SIZE;
}

} // namespace

EvSession::EvSession(std::unique_ptr<io::IConnection> connection_, session::EvSessionConfig session_config,
                     const session::ev::feedback::Callbacks& callbacks_) :
    connection(std::move(connection_)),
    log(this),
    config(std::move(session_config)),
    callbacks(callbacks_),
    feedback(callbacks_) {

    next_session_event = offset_time_point_by_ms(get_current_time_point(), SESSION_IDLE_TIMEOUT_MS);
    connection->set_event_callback([this](io::ConnectionEvent event) { this->handle_connection_event(event); });
}

EvSession::~EvSession() = default;

bool EvSession::is_finished() const {
    if (engine) {
        return driver_stopped or engine->is_finished();
    }
    return driver_stopped;
}

void EvSession::push_control_event(const d20::ev::ControlEvent& event) {
    control_event_queue.push(event);
}

TimePoint const& EvSession::poll() {
    const auto now = get_current_time_point();
    next_session_event = offset_time_point_by_ms(now, SESSION_IDLE_TIMEOUT_MS);

    if (not state.connected) {
        return next_session_event;
    }

    if (state.new_data) {
        const bool would_block = read_packet(*connection, packet);
        if (would_block) {
            state.new_data = false;
        }
    }

    // deliver queued control events to the engine (dropped during the handshake, which ignores them)
    while (const auto event = control_event_queue.pop()) {
        if (engine) {
            engine->on_control_event(event.value());
        }
    }

    // check timeouts
    const auto timeouts_reached = timeouts.check();
    if (timeouts_reached.has_value()) {
        for (const auto& timeout : timeouts_reached.value()) {
            if (engine) {
                engine->on_timeout(timeout);
            } else {
                log("SupportedAppProtocol message timeout reached, terminating session");
                driver_stopped = true;
            }
            timeouts.reset_timeout(timeout);
        }
    }

    // handle a complete incoming packet
    if (packet.is_complete()) {
        log_packet_from_secc(packet, log);

        const auto payload_type = packet.get_payload_type();
        const io::StreamInputView view{packet.get_payload_buffer(), packet.get_payload_length()};

        if (engine) {
            engine->on_packet(payload_type, view);
        } else {
            handle_supported_app_protocol_response(payload_type, view);
        }

        packet = {};
    }

    // send a pending request, paced to at least MIN_REQUEST_INTERVAL_MS apart
    const bool has_outgoing = pending_sap_outgoing.has_value() or (engine and engine->has_outgoing());
    if (has_outgoing) {
        if (not request_send_after.has_value()) {
            request_send_after = now;
            if (last_request_tx_time.has_value()) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    get_current_time_point() - last_request_tx_time.value());
                if (elapsed < std::chrono::milliseconds(d20::ev::MIN_REQUEST_INTERVAL_MS)) {
                    request_send_after =
                        offset_time_point_by_ms(last_request_tx_time.value(), d20::ev::MIN_REQUEST_INTERVAL_MS);
                }
            }
        }

        if (request_send_after.has_value() && now < request_send_after.value()) {
            next_session_event = request_send_after.value();
        } else {
            request_send_after.reset();
            send_request();
        }
    } else {
        request_send_after.reset();
    }

    if (is_finished() and not state.teardown_signaled) {
        state.teardown_signaled = true;
        // The SECC keeps the TCP connection open ~5 s after SessionStopRes [V2G20-1643]; closing from
        // our side after receiving the response is correct.
        connection->close();
        // Publish v2g_session_finished exactly once per session, on any termination path [flow spec §4.5].
        feedback.v2g_session_finished();
        const bool paused = engine and engine->is_paused();
        const auto signal =
            paused ? session::ev::feedback::Signal::DLINK_PAUSE : session::ev::feedback::Signal::DLINK_TERMINATE;
        feedback.signal(signal);
    }

    return next_session_event;
}

void EvSession::start_supported_app_protocol() {
    const auto req = d20::ev::state::supported_app_protocol::create_request(
        config.supported_protocols, config.supported_energy_services, config.custom_protocol);

    if (req.app_protocol.empty()) {
        log("No application protocols to offer (empty SupportedAppProtocolReq); check that the configured "
            "protocols match the requested energy service (e.g. DIN SPEC 70121 is DC-only). Terminating session");
        driver_stopped = true;
        return;
    }

    offered_protocols.clear();
    for (const auto& proto : req.app_protocol) {
        if (const auto protocol_id = protocol_id_from_namespace(proto.protocol_namespace)) {
            offered_protocols.push_back({proto.schema_id, protocol_id.value()});
        }
    }

    const io::StreamOutputView view{request_buffer + io::SdpPacket::V2GTP_HEADER_SIZE,
                                    sizeof(request_buffer) - io::SdpPacket::V2GTP_HEADER_SIZE};
    const auto payload_size = message_20::serialize(req, view);

    pending_sap_outgoing =
        PendingOutgoing{payload_size, io::v2gtp::PayloadType::SAP, message_20::Type::SupportedAppProtocolReq};
    timeouts.start_timeout(d20::TimeoutType::SEQUENCE, d20::ev::MESSAGE_TIMEOUT_MS);
}

void EvSession::handle_supported_app_protocol_response(io::v2gtp::PayloadType payload_type,
                                                       const io::StreamInputView& view) {
    timeouts.stop_timeout(d20::TimeoutType::SEQUENCE);

    const message_20::Variant variant{payload_type, view};
    feedback.v2g_message(variant.get_type());

    const auto res = variant.get_if<message_20::SupportedAppProtocolResponse>();
    if (res == nullptr) {
        log("expected SupportedAppProtocolRes! But code type id: %d", variant.get_type());
        driver_stopped = true;
        return;
    }

    const auto result = d20::ev::state::supported_app_protocol::handle_response(*res);
    if (not result.negotiated or not result.schema_id.has_value()) {
        log("SupportedAppProtocol negotiation failed, terminating session");
        driver_stopped = true;
        return;
    }

    const auto offered = std::find_if(offered_protocols.begin(), offered_protocols.end(),
                                      [&](const OfferedProtocol& o) { return o.schema_id == result.schema_id.value(); });
    if (offered == offered_protocols.end()) {
        log("SupportedAppProtocol negotiated an unknown schema, terminating session");
        driver_stopped = true;
        return;
    }

    engine = create_engine(offered->protocol);
    if (not engine) {
        driver_stopped = true;
        return;
    }
    selected_protocol_id = offered->protocol;

    engine->kick_first_request();
}

std::unique_ptr<EvEngine> EvSession::create_engine(ProtocolId protocol_id) {
    switch (protocol_id) {
    case ProtocolId::ISO15118_20: {
        // The selected protocol is derived from the EV's highest-priority offered service.
        const auto& services = config.supported_energy_services;
        const bool is_dc = std::any_of(services.begin(), services.end(), [](dt::ServiceCategory s) {
            return s == dt::ServiceCategory::DC or s == dt::ServiceCategory::DC_BPT or
                   s == dt::ServiceCategory::MCS or s == dt::ServiceCategory::MCS_BPT;
        });
        feedback.selected_protocol(is_dc ? "ISO15118-20:DC" : "ISO15118-20:AC");

        const io::StreamOutputView view{request_buffer + io::SdpPacket::V2GTP_HEADER_SIZE,
                                        sizeof(request_buffer) - io::SdpPacket::V2GTP_HEADER_SIZE};
        return std::make_unique<D20EvEngine>(view, config, callbacks, log, timeouts);
    }
    case ProtocolId::ISO15118_2: {
        // The AC/DC branch and the selected_protocol feedback are derived by the ISO-2 state machine
        // from the requested energy transfer mode (see d2::ev::state::ServiceDiscovery).
        d2::ev::EvSessionConfig d2_config{};
        // Only override the EVCCID when a real MAC was resolved; an all-zero value means the module
        // could not read one, so keep the library's deliberate non-zero default.
        if (config.evcc_mac != std::array<uint8_t, 6>{}) {
            d2_config.evcc_mac = config.evcc_mac;
        }
        d2_config.requested_energy_transfer_mode = config.iso2_energy_transfer_mode;
        d2_config.ac_e_amount = config.iso2_ac_e_amount;
        d2_config.ac_ev_max_voltage = config.iso2_ac_ev_max_voltage;
        d2_config.ac_ev_max_current = config.iso2_ac_ev_max_current;
        d2_config.ac_ev_min_current = config.iso2_ac_ev_min_current;

        const auto& dc = config.dc_charge_parameters;
        d2_config.dc_ev_max_voltage = dt::from_RationalNumber(dc.max_voltage);
        d2_config.dc_ev_max_current = dt::from_RationalNumber(dc.max_charge_current);
        d2_config.dc_target_voltage = dt::from_RationalNumber(dc.target_voltage);
        d2_config.dc_target_current = dt::from_RationalNumber(dc.target_current);
        d2_config.dc_energy_capacity = dt::from_RationalNumber(dc.energy_capacity);
        d2_config.resumed_session_id = config.resumed_session_id;

        const io::StreamOutputView view{request_buffer + io::SdpPacket::V2GTP_HEADER_SIZE,
                                        sizeof(request_buffer) - io::SdpPacket::V2GTP_HEADER_SIZE};
        return std::make_unique<D2EvEngine>(view, std::move(d2_config), callbacks, log, timeouts);
    }
    case ProtocolId::DIN70121: {
        feedback.selected_protocol("DIN70121");

        // Map the DC parameters from the (d20) session config into the DIN config.
        din::ev::EvSessionConfig din_config{};
        // See the ISO 15118-2 branch: keep the library default EVCCID when no MAC was resolved.
        if (config.evcc_mac != std::array<uint8_t, 6>{}) {
            din_config.evcc_mac = config.evcc_mac;
        }
        // message_2 and message_din share one EnergyTransferMode type (iso15118::datatypes), so this
        // is a plain assignment rather than a cross-enum cast.
        din_config.requested_energy_transfer_type = config.iso2_energy_transfer_mode;
        if (config.resumed_session_id.has_value()) {
            din_config.resumed_session_id = config.resumed_session_id.value();
        }

        const auto& dc = config.dc_charge_parameters;
        din_config.dc.max_current_limit = dt::from_RationalNumber(dc.max_charge_current);
        din_config.dc.max_voltage_limit = dt::from_RationalNumber(dc.max_voltage);
        din_config.dc.max_power_limit = dt::from_RationalNumber(dc.max_charge_power);
        din_config.dc.target_voltage = dt::from_RationalNumber(dc.target_voltage);
        din_config.dc.target_current = dt::from_RationalNumber(dc.target_current);
        din_config.dc.energy_capacity = dt::from_RationalNumber(dc.energy_capacity);

        const io::StreamOutputView view{request_buffer + io::SdpPacket::V2GTP_HEADER_SIZE,
                                        sizeof(request_buffer) - io::SdpPacket::V2GTP_HEADER_SIZE};
        return std::make_unique<DinEvEngine>(view, std::move(din_config), callbacks, log, timeouts);
    }
    }
    return nullptr;
}

bool EvSession::is_paused() const {
    return engine and engine->is_paused();
}

std::optional<std::array<uint8_t, 8>> EvSession::session_id() const {
    if (not engine) {
        return std::nullopt;
    }
    return engine->session_id();
}

std::optional<ProtocolId> EvSession::selected_protocol() const {
    return selected_protocol_id;
}

void EvSession::send_request() {
    size_t payload_size;
    io::v2gtp::PayloadType payload_type;
    V2gMessageType message_type;

    if (pending_sap_outgoing.has_value()) {
        payload_size = pending_sap_outgoing->payload_size;
        payload_type = pending_sap_outgoing->payload_type;
        message_type = pending_sap_outgoing->message_type;
        pending_sap_outgoing.reset();
    } else if (engine) {
        const auto outgoing = engine->take_outgoing();
        if (not outgoing.has_value()) {
            return;
        }
        payload_size = outgoing->payload_size;
        payload_type = outgoing->payload_type;
        message_type = outgoing->message_type;
    } else {
        return;
    }

    const auto request_size = setup_request_header(request_buffer, payload_type, payload_size);
    connection->write(request_buffer, request_size);
    last_request_tx_time = get_current_time_point();

    log.exi(static_cast<uint16_t>(payload_type), request_buffer + io::SdpPacket::V2GTP_HEADER_SIZE, payload_size,
            session::logging::ExiMessageDirection::FROM_EV);

    feedback.v2g_message(message_type);
}

void EvSession::handle_connection_event(io::ConnectionEvent event) {
    using Event = io::ConnectionEvent;
    switch (event) {
    case Event::ACCEPTED:
        state.connected = true;
        log("Connection accepted to SECC");
        return;

    case Event::OPEN:
        state.connected = true;
        if (not state.initial_request_sent) {
            state.initial_request_sent = true;
            // Kick off the handshake with the SupportedAppProtocol request.
            start_supported_app_protocol();
        }
        return;

    case Event::NEW_DATA:
        state.new_data = true;
        return;

    case Event::CLOSED:
        state.connected = false;
        logf_info("Connection is closed");
        return;
    }
}

void EvSession::close() {
    connection->close();
    feedback.signal(session::ev::feedback::Signal::DLINK_TERMINATE);
    driver_stopped = true;
}

} // namespace iso15118
