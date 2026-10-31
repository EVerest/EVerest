// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/session/sap_offer.hpp>

#include <algorithm>
#include <string_view>

#include <iso15118/ev/d20/service_family.hpp>

namespace iso15118::ev::session {

namespace {

// A DER service offers only its AMD1 namespace, with no -20:AC fallback: an SECC without AMD1 cannot serve it.
std::string_view iso20_namespace(message_20::datatypes::ServiceCategory service) {
    if (service == message_20::datatypes::ServiceCategory::AC_DER_IEC) {
        return ISO20_AC_DER_IEC_PROTOCOL_NAMESPACE;
    }
    if (service == message_20::datatypes::ServiceCategory::AC_DER_SAE) {
        return ISO20_AC_DER_SAE_PROTOCOL_NAMESPACE;
    }
    return d20::is_ac_family(service) ? ISO20_AC_PROTOCOL_NAMESPACE : ISO20_DC_PROTOCOL_NAMESPACE;
}

// -20 over TLS 1.3, -2 over TLS 1.2; no negotiated version keeps every protocol.
bool runs_over(ProtocolId protocol, std::optional<config::TlsVersion> negotiated) {
    if (not negotiated.has_value()) {
        return true;
    }
    if (protocol == ProtocolId::ISO15118_20) {
        return negotiated == config::TlsVersion::Tls1_3;
    }
    return protocol == ProtocolId::ISO15118_2 and negotiated == config::TlsVersion::Tls1_2;
}

} // namespace

std::vector<OfferedProtocol> build_sap_offer(const SapOfferInput& input) {
    std::vector<OfferedProtocol> offer;
    uint8_t counter = 1;

    const auto add = [&](const std::string& ns, uint32_t major, uint32_t minor, ProtocolId protocol) {
        if (not runs_over(protocol, input.negotiated_tls)) {
            return;
        }
        const auto known = std::any_of(offer.begin(), offer.end(),
                                       [&](const OfferedProtocol& o) { return o.entry.protocol_namespace == ns; });
        if (known) {
            return;
        }
        offer.push_back({{ns, major, minor, counter, counter}, protocol});
        ++counter;
    };

    const bool ac = d20::is_ac_family(input.energy_service);

    for (const auto protocol : input.supported_protocols) {
        if (input.resume_protocol.has_value() and protocol != input.resume_protocol.value()) {
            continue;
        }
        switch (protocol) {
        case ProtocolId::ISO15118_20:
            add(std::string(iso20_namespace(input.energy_service)), 1, 0, ProtocolId::ISO15118_20);
            break;
        case ProtocolId::ISO15118_2:
            add(ISO2_NAMESPACE, 2, 0, ProtocolId::ISO15118_2);
            break;
        case ProtocolId::DIN70121:
            if (not ac and not input.tls) {
                add(DIN70121_NAMESPACE, 2, 0, ProtocolId::DIN70121);
            }
            break;
        }
    }

    if (input.custom_protocol.has_value() and not input.resume_protocol.has_value()) {
        const auto resolved =
            protocol_id_from_namespace(input.custom_protocol.value()).value_or(ProtocolId::ISO15118_20);
        // ISO 15118-2 and DIN SPEC 70121 are version 2.0 on the wire, ISO 15118-20 is 1.0.
        const uint32_t major = (resolved == ProtocolId::ISO15118_2 or resolved == ProtocolId::DIN70121) ? 2 : 1;
        add(input.custom_protocol.value(), major, 0, resolved);
    }

    return offer;
}

std::vector<OfferedProtocol> offer_from_app_protocols(const std::vector<message_20::SupportedAppProtocol>& list) {
    std::vector<OfferedProtocol> offer;
    for (const auto& entry : list) {
        if (const auto protocol = protocol_id_from_namespace(entry.protocol_namespace)) {
            offer.push_back({entry, protocol.value()});
        }
    }
    return offer;
}

std::vector<OfferedProtocol> offer_for_config(const config::EvConfig& config,
                                              std::optional<config::TlsVersion> negotiated_tls) {
    if (not config.advertised_app_protocols.empty()) {
        return offer_from_app_protocols(config.advertised_app_protocols);
    }
    SapOfferInput input;
    input.supported_protocols = config.supported_protocols;
    input.energy_service = config.energy_service;
    input.tls = config.tls.mode != config::TlsMode::Off;
    input.custom_protocol = config.custom_protocol;
    if (config.resume.has_value()) {
        input.resume_protocol = config.resume->protocol;
    }
    if (config.tls.version == config::TlsVersion::Auto) {
        input.negotiated_tls = negotiated_tls;
    }
    return build_sap_offer(input);
}

AutoTlsVersions auto_tls_versions(const config::EvConfig& config) {
    AutoTlsVersions versions;
    for (const auto& entry : offer_for_config(config)) {
        versions.tls_1_2 = versions.tls_1_2 or entry.protocol == ProtocolId::ISO15118_2;
        versions.tls_1_3 = versions.tls_1_3 or entry.protocol == ProtocolId::ISO15118_20;
    }
    return versions;
}

} // namespace iso15118::ev::session
