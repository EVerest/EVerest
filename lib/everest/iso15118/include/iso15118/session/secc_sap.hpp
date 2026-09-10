// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <iso15118/message/common_types.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/session/protocol.hpp>

// The SECC counterpart of the EVCC create_request / handle_response free functions in ev_sap.cpp.
namespace iso15118::session::secc_sap {

namespace dt = message_20::datatypes;

struct HandleResult {
    message_20::SupportedAppProtocolResponse response;
    std::optional<std::string> selected_namespace{std::nullopt};
};

// Picks the highest-priority match [V2G20-167]. Only generations present in supported_protocols are
// accepted; the custom protocol is always accepted when offered.
// \p tls_active gates plaintext-only protocols: DIN SPEC 70121 is not offered over TLS
// [V2G-DC-869]. The converse is NOT enforced -- ISO 15118-20 mandates TLS [V2G20-2677] but is still
// negotiated on a plain connection so bring-up and ENFORCE_NO_TLS deployments keep working.
HandleResult handle_request(const message_20::SupportedAppProtocolRequest& req,
                            const std::vector<ProtocolId>& supported_protocols,
                            const std::vector<dt::ServiceCategory>& supported_energy_services,
                            bool selecting_sap_based_on_energy_service,
                            const std::optional<std::string>& custom_protocol, bool tls_active);

// The -20 AC/DC/DER namespaces and an agreed custom namespace all run on the -20 engine.
std::optional<ProtocolId> protocol_id_from_selected_namespace(const std::string& selected_namespace,
                                                              const std::optional<std::string>& custom_protocol);

} // namespace iso15118::session::secc_sap
