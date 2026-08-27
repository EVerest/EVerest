// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/sap_engine.hpp>

#include <string>
#include <utility>

#include <iso15118/session/secc_sap.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/message/variant.hpp>

namespace iso15118 {

SapEngine::SapEngine(io::StreamOutputView output_view_, const session::SessionConfig& config,
                     session::feedback::Callbacks callbacks, bool tls_active_) :
    output_view(output_view_),
    feedback(std::move(callbacks)),
    supported_protocols(config.supported_protocols),
    supported_energy_services(config.supported_energy_transfer_services),
    selecting_sap_based_on_energy_service(config.selecting_sap_based_on_energy_service),
    custom_protocol(config.custom_protocol),
    tls_active(tls_active_) {
}

void SapEngine::on_packet(io::v2gtp::PayloadType payload_type, const io::StreamInputView& view) {
    const message_20::Variant variant{payload_type, view};
    feedback.v2g_message(variant.get_type());

    const auto req = variant.get_if<message_20::SupportedAppProtocolRequest>();
    if (req == nullptr) {
        logf_warning("Expected SupportedAppProtocolReq! But code type id: %d", variant.get_type());
        stopped = true;
        return;
    }

    if (selecting_sap_based_on_energy_service) {
        logf_info("Selecting supported app protocol namespace based on the supported energy services");
    }

    const auto result =
        session::secc_sap::handle_request(*req, supported_protocols, supported_energy_services,
                                          selecting_sap_based_on_energy_service, custom_protocol, tls_active);

    const auto payload_size = message_20::serialize(result.response, output_view);
    outgoing = SeccOutgoing{payload_size, io::v2gtp::PayloadType::SAP, message_20::Type::SupportedAppProtocolRes};

    if (result.response.response_code == message_20::SupportedAppProtocolResponse::ResponseCode::Failed_NoNegotiation) {
        std::string ev_supported_namespaces{};
        for (const auto& protocol : req->app_protocol) {
            ev_supported_namespaces.append(protocol.protocol_namespace + ";");
        }
        logf_error("Selecting a protocol namespace failed. Ev offered: %s", ev_supported_namespaces.c_str());
        stopped = true;
        return;
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
    } else if (custom_protocol.has_value() and custom_protocol.value() == selected_namespace) {
        feedback.selected_protocol(custom_protocol.value());
        logf_warning("EV and EVSE have agreed on a custom protocol namespace. Problems or aborts can occur in the "
                     "following states!");
    } else {
        feedback.selected_protocol("ISO15118-20:AC and similar");
    }

    const auto protocol_id =
        session::secc_sap::protocol_id_from_selected_namespace(selected_namespace, custom_protocol);
    if (not protocol_id.has_value()) {
        logf_error("SupportedAppProtocol negotiated an unknown namespace, terminating session");
        stopped = true;
        return;
    }

    Negotiated result_negotiated{protocol_id.value(), req->app_protocol, {}};
    for (const auto& protocol : req->app_protocol) {
        if (result.response.schema_id.has_value() and protocol.schema_id == result.response.schema_id.value()) {
            result_negotiated.selected_protocol = protocol;
        }
    }
    negotiated = std::move(result_negotiated);
}

void SapEngine::on_control_event(const d20::ControlEvent&) {
    // No session exists yet, so there is nothing a charger-side event could act on. Events pushed
    // during the handshake are dropped here (the module re-reports the ones that matter -- limits,
    // errors, CP state -- to the protocol engine once the session runs).
}

void SapEngine::on_timeout(d20::TimeoutType) {
    // The only timeout armed during the handshake is the sequence timeout guarding the wait for the
    // SupportedAppProtocolReq (armed by the Session on TCP accept).
    logf_error("Timeout reached during SupportedAppProtocol handshake. Stopping the session");
    stopped = true;
}

bool SapEngine::has_outgoing() const {
    return outgoing.has_value();
}

std::optional<SeccOutgoing> SapEngine::take_outgoing() {
    return std::exchange(outgoing, std::nullopt);
}

bool SapEngine::is_finished() const {
    // Mirrors the protocol engines: never report finished while a response is still staged, so a
    // FAILED_NoNegotiation still reaches the EV before the session is torn down.
    return stopped and not has_outgoing();
}

void SapEngine::request_shutdown() {
    // There is no V2G session to end gracefully yet -- a SessionStop needs a running state machine.
    // The controller's hard shutdown path (Session::close()) tears the connection down instead.
    logf_info("Shutdown requested during the SupportedAppProtocol handshake; no session to stop yet");
}

std::optional<SapEngine::Negotiated> SapEngine::take_negotiated() {
    return std::exchange(negotiated, std::nullopt);
}

} // namespace iso15118
