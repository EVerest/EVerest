// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/protocol.hpp>

#include <algorithm>
#include <vector>

#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/d20/ev/state/supported_app_protocol.hpp>

namespace iso15118::d20::ev::state {

using ResponseCode = message_20::SupportedAppProtocolResponse::ResponseCode;

namespace supported_app_protocol {

// is_dc_service / is_ac_service are reused from detail/d20/ev/state/state_helper.hpp.

message_20::SupportedAppProtocolRequest create_request(const std::vector<ProtocolId>& supported_protocols,
                                                       const std::vector<dt::ServiceCategory>& supported_energy_services,
                                                       const std::optional<std::string>& custom_protocol) {
    message_20::SupportedAppProtocolRequest req;

    std::vector<std::string> namespaces;
    uint8_t counter = 1; // schema_id and priority both start at 1

    const auto add_namespace = [&](const std::string& ns, uint32_t major, uint32_t minor) {
        if (std::find(namespaces.begin(), namespaces.end(), ns) != namespaces.end()) {
            return;
        }
        namespaces.push_back(ns);
        req.app_protocol.push_back({ns, major, minor, counter, counter});
        ++counter;
    };

    const bool any_dc =
        std::any_of(supported_energy_services.begin(), supported_energy_services.end(), is_dc_service);
    const bool any_ac =
        std::any_of(supported_energy_services.begin(), supported_energy_services.end(), is_ac_service);

    for (const auto protocol : supported_protocols) {
        switch (protocol) {
        case ProtocolId::ISO15118_20:
            // Derive the -20 namespace(s) from the requested energy services (version encoded in the
            // namespace itself, VersionNumberMajor 1).
            if (any_dc) {
                add_namespace(ISO20_DC_PROTOCOL_NAMESPACE, 1, 0);
            }
            if (any_ac) {
                add_namespace(ISO20_AC_PROTOCOL_NAMESPACE, 1, 0);
            }
            break;
        case ProtocolId::ISO15118_2:
            add_namespace(ISO2_NAMESPACE, 2, 0);
            break;
        case ProtocolId::DIN70121:
            // DIN SPEC 70121 is DC-only; skip it for an AC-only service.
            if (any_dc) {
                add_namespace(DIN70121_NAMESPACE, 2, 0);
            }
            break;
        }
    }

    if (custom_protocol.has_value()) {
        add_namespace(custom_protocol.value(), 1, 0);
    }

    return req;
}

Result handle_response(const message_20::SupportedAppProtocolResponse& res) {
    const bool negotiated = (res.response_code == ResponseCode::OK_SuccessfulNegotiation) or
                            (res.response_code == ResponseCode::OK_SuccessfulNegotiationWithMinorDeviation);
    return {negotiated, res.schema_id};
}

} // namespace supported_app_protocol

} // namespace iso15118::d20::ev::state
