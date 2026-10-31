// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/ev/config/config.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/session/protocol.hpp>

namespace iso15118::ev::session {

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
    // TLS version negotiated under tls_version Auto: 1.3 keeps only -20, 1.2 only -2.
    std::optional<config::TlsVersion> negotiated_tls{std::nullopt};
};

// Builds the SAP offer: one entry per supported protocol in priority order, DIN skipped under TLS,
// only the protocol matching negotiated_tls when it is set, schema_id == priority, numbered from 1.
// Empty when nothing can be offered.
std::vector<OfferedProtocol> build_sap_offer(const SapOfferInput& input);

// Adopts an explicit SupportedAppProtocol list (tests, custom setups); entries whose namespace is
// unknown map to no protocol and are dropped.
std::vector<OfferedProtocol> offer_from_app_protocols(const std::vector<message_20::SupportedAppProtocol>& list);

// The offer a Controller built from `config` sends: its advertised_app_protocols when set, else
// build_sap_offer over its protocols, energy service, transport and resume, narrowed to
// `negotiated_tls` when its tls version is Auto.
std::vector<OfferedProtocol> offer_for_config(const config::EvConfig& config,
                                              std::optional<config::TlsVersion> negotiated_tls = std::nullopt);

// The TLS versions tls_version Auto offers: 1.3 when the pre-handshake offer holds an ISO 15118-20
// entry, 1.2 when it holds an ISO 15118-2 entry.
struct AutoTlsVersions {
    bool tls_1_2{false};
    bool tls_1_3{false};
};
AutoTlsVersions auto_tls_versions(const config::EvConfig& config);

} // namespace iso15118::ev::session
