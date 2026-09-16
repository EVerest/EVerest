// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/message/common_types.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/session/protocol.hpp>

namespace iso15118::ev {

// One SupportedAppProtocolReq entry and the protocol generation its schema_id selects.
struct OfferedProtocol {
    message_20::SupportedAppProtocol entry;
    ProtocolId protocol;
};

struct SapOfferInput {
    // Priority order, lower index = higher priority.
    std::vector<ProtocolId> supported_protocols{ProtocolId::ISO15118_20};
    message_20::datatypes::ServiceCategory energy_service{message_20::datatypes::ServiceCategory::DC};
    // TLS transport: DIN SPEC 70121 is not offered [V2G-DC-868].
    bool tls{false};
    std::optional<std::string> custom_protocol{std::nullopt};
    // Resuming a paused session: the offer is constrained to that protocol.
    std::optional<ProtocolId> resume_protocol{std::nullopt};
};

// Builds the SAP offer: -20 namespace (AC/DC) from the energy service, DIN skipped for AC services and
// under TLS, schema_id == priority, numbered from 1. Empty when nothing can be offered.
std::vector<OfferedProtocol> build_sap_offer(const SapOfferInput& input);

// Adopts an explicit SupportedAppProtocol list (tests, custom setups); entries whose namespace is
// unknown map to no protocol and are dropped.
std::vector<OfferedProtocol> offer_from_app_protocols(const std::vector<message_20::SupportedAppProtocol>& list);

} // namespace iso15118::ev
