// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/sap_offer.hpp>

#include <algorithm>

#include <iso15118/ev/service_family.hpp>

namespace iso15118::ev {

std::vector<OfferedProtocol> build_sap_offer(const SapOfferInput& input) {
    std::vector<OfferedProtocol> offer;
    uint8_t counter = 1;

    const auto add = [&](const std::string& ns, uint32_t major, uint32_t minor, ProtocolId protocol) {
        const auto known = std::any_of(offer.begin(), offer.end(),
                                       [&](const OfferedProtocol& o) { return o.entry.protocol_namespace == ns; });
        if (known) {
            return;
        }
        offer.push_back({{ns, major, minor, counter, counter}, protocol});
        ++counter;
    };

    const bool ac = is_ac_family(input.energy_service);

    for (const auto protocol : input.supported_protocols) {
        if (input.resume_protocol.has_value() and protocol != input.resume_protocol.value()) {
            continue;
        }
        switch (protocol) {
        case ProtocolId::ISO15118_20:
            add(ac ? ISO20_AC_PROTOCOL_NAMESPACE : ISO20_DC_PROTOCOL_NAMESPACE, 1, 0, ProtocolId::ISO15118_20);
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

} // namespace iso15118::ev
