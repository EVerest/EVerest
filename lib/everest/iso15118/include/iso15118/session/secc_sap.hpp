// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <iso15118/message/common_types.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/session/protocol.hpp>

// SECC-side SupportedAppProtocol negotiation. The handshake is protocol-independent and is run by the
// Session driver before any engine exists; this is the SECC counterpart of the EVCC create_request /
// handle_response free functions in ev_sap.cpp.
namespace iso15118::session::secc_sap {

namespace dt = message_20::datatypes;

struct HandleResult {
    message_20::SupportedAppProtocolResponse response;
    // Namespace of the negotiated protocol (only set on a successful negotiation).
    std::optional<std::string> selected_namespace{std::nullopt};
};

// Matches the EV's offered protocols against the SECC's configured supported set and (optionally) the
// supported energy services, picks the highest-priority match [V2G20-167] and builds the response. Only
// protocol generations present in supported_protocols are accepted; the custom protocol is always
// accepted when offered.
HandleResult handle_request(const message_20::SupportedAppProtocolRequest& req,
                            const std::vector<ProtocolId>& supported_protocols,
                            const std::vector<dt::ServiceCategory>& supported_energy_services,
                            bool selecting_sap_based_on_energy_service,
                            const std::optional<std::string>& custom_protocol);

// Maps a negotiated SupportedAppProtocol namespace to the ProtocolId whose engine should run it. The
// ISO 15118-20 AC/DC/DER namespaces and an agreed custom namespace all run on the -20 engine.
std::optional<ProtocolId> protocol_id_from_selected_namespace(const std::string& selected_namespace,
                                                              const std::optional<std::string>& custom_protocol);

} // namespace iso15118::session::secc_sap
