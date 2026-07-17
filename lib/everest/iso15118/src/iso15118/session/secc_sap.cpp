// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/secc_sap.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <utility>

namespace iso15118::session::secc_sap {

using ResponseCode = message_20::SupportedAppProtocolResponse::ResponseCode;

namespace {

// The DC/AC -20 namespace literals have their single source in session/protocol.hpp; the DER variants
// are specific to this SECC handler (they run on the -20 engine).
constexpr auto ISO20_DER_IEC_NAMESPACE = "urn:iso:std:iso:15118:-20:AC-DER-IEC";
constexpr auto ISO20_DER_SAE_NAMESPACE = "urn:iso:std:iso:15118:-20:AC-DER-SAE";

constexpr std::array AcNamespaces = {ISO20_AC_PROTOCOL_NAMESPACE, ISO20_DER_IEC_NAMESPACE, ISO20_DER_SAE_NAMESPACE};

struct SupportedEnergyModes {
    bool ac{false};
    bool dc{false};
    bool acdp{false};
    bool wpt{false};
};

bool protocol_supported(ProtocolId id, const std::vector<ProtocolId>& supported_protocols) {
    return std::find(supported_protocols.begin(), supported_protocols.end(), id) != supported_protocols.end();
}

bool is_ac_namespace(const std::string& protocol_namespace) {
    return std::find(AcNamespaces.begin(), AcNamespaces.end(), protocol_namespace) != AcNamespaces.end();
}

// The protocol version this SECC implements for a given namespace, used to
// decide OK_SuccessfulNegotiation vs OK_SuccessfulNegotiationWithMinorDeviation
// ([V2G2-098]/[V2G20-149]): when the EV's matched protocol has the same major
// but a different minor version, the SECC must answer with the minor-deviation
// code. Returns nullopt for namespaces without a well-known version (e.g. a
// custom protocol), in which case no deviation is reported.
std::optional<std::pair<uint32_t, uint32_t>> secc_protocol_version(const std::string& protocol_namespace) {
    if (protocol_namespace == ISO2_NAMESPACE) {
        return std::pair<uint32_t, uint32_t>{2, 0}; // ISO 15118-2:2013
    }
    if (protocol_namespace == DIN70121_NAMESPACE) {
        return std::pair<uint32_t, uint32_t>{2, 0}; // DIN SPEC 70121:2014
    }
    if (protocol_namespace == ISO20_DC_PROTOCOL_NAMESPACE or is_ac_namespace(protocol_namespace)) {
        return std::pair<uint32_t, uint32_t>{1, 0}; // ISO 15118-20:2022
    }
    return std::nullopt;
}

} // namespace

HandleResult handle_request(const message_20::SupportedAppProtocolRequest& req,
                            const std::vector<ProtocolId>& supported_protocols,
                            const std::vector<dt::ServiceCategory>& supported_energy_services,
                            bool selecting_sap_based_on_energy_service,
                            const std::optional<std::string>& custom_protocol) {
    SupportedEnergyModes energy_modes{false, false, false, false};

    if (selecting_sap_based_on_energy_service) {
        for (const auto& service : supported_energy_services) {
            if (service == dt::ServiceCategory::AC or service == dt::ServiceCategory::AC_BPT) {
                energy_modes.ac = true;
            } else if (service == dt::ServiceCategory::DC or service == dt::ServiceCategory::DC_BPT or
                       service == dt::ServiceCategory::MCS or service == dt::ServiceCategory::MCS_BPT) {
                energy_modes.dc = true;
            } else if (service == dt::ServiceCategory::DC_ACDP or service == dt::ServiceCategory::DC_ACDP_BPT) {
                energy_modes.acdp = true;
            } else if (service == dt::ServiceCategory::WPT) {
                energy_modes.wpt = true;
            }
        }
    } else {
        energy_modes = SupportedEnergyModes{true, true, true, true};
    }

    const bool iso20_supported = protocol_supported(ProtocolId::ISO15118_20, supported_protocols);
    const bool iso2_supported = protocol_supported(ProtocolId::ISO15118_2, supported_protocols);
    const bool din_supported = protocol_supported(ProtocolId::DIN70121, supported_protocols);

    std::map<uint8_t, uint8_t> ev_supported_protocols{};   // key: priority, value: schema_id
    std::map<uint8_t, std::string> ev_supported_namespaces{}; // key: priority, value: namespace
    std::map<uint8_t, std::pair<uint32_t, uint32_t>> ev_supported_versions{}; // key: priority, value: (major, minor)

    for (const auto& protocol : req.app_protocol) {
        const auto is_dc =
            iso20_supported and protocol.protocol_namespace == ISO20_DC_PROTOCOL_NAMESPACE and energy_modes.dc;
        const auto is_ac = iso20_supported and is_ac_namespace(protocol.protocol_namespace) and energy_modes.ac;
        // ISO 15118-2 covers both AC and DC under a single namespace.
        const auto is_iso2 =
            iso2_supported and protocol.protocol_namespace == ISO2_NAMESPACE and (energy_modes.ac or energy_modes.dc);
        // DIN SPEC 70121 is DC only.
        const auto is_din =
            din_supported and protocol.protocol_namespace == DIN70121_NAMESPACE and energy_modes.dc;
        const auto is_custom = custom_protocol.has_value() ? protocol.protocol_namespace == custom_protocol.value()
                                                           : false;

        if (is_dc or is_ac or is_iso2 or is_din or is_custom) {
            ev_supported_protocols[protocol.priority] = protocol.schema_id;
            ev_supported_namespaces[protocol.priority] = protocol.protocol_namespace;
            ev_supported_versions[protocol.priority] = {protocol.version_number_major,
                                                        protocol.version_number_minor};
        }
    }

    HandleResult result;

    if (ev_supported_protocols.empty()) {
        result.response.response_code = ResponseCode::Failed_NoNegotiation;
        return result;
    }

    // [V2G20-167] Highest Prio: 1, Lowest Prio: 20
    result.response.schema_id = ev_supported_protocols.begin()->second;
    result.selected_namespace = ev_supported_namespaces.begin()->second;

    // [V2G2-098] / [V2G20-149]: when the selected protocol matches the SECC's
    // namespace and major version but the minor version differs, negotiation
    // succeeds with a minor deviation. (On par with the EvseV2G stack.)
    const auto& [offered_major, offered_minor] = ev_supported_versions.begin()->second;
    const auto secc_version = secc_protocol_version(result.selected_namespace.value());
    if (secc_version.has_value() and offered_major == secc_version->first and
        offered_minor != secc_version->second) {
        result.response.response_code = ResponseCode::OK_SuccessfulNegotiationWithMinorDeviation;
    } else {
        result.response.response_code = ResponseCode::OK_SuccessfulNegotiation;
    }
    return result;
}

std::optional<ProtocolId> protocol_id_from_selected_namespace(const std::string& selected_namespace,
                                                              const std::optional<std::string>& custom_protocol) {
    if (custom_protocol.has_value() and selected_namespace == custom_protocol.value()) {
        return ProtocolId::ISO15118_20;
    }
    if (is_ac_namespace(selected_namespace)) {
        // Covers the plain -20 AC namespace as well as the AC-DER-IEC / AC-DER-SAE variants.
        return ProtocolId::ISO15118_20;
    }
    return protocol_id_from_namespace(selected_namespace);
}

} // namespace iso15118::session::secc_sap
