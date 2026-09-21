// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

namespace iso15118 {

// ISO 15118 protocol generation negotiated during the SupportedAppProtocol handshake.
enum class ProtocolId {
    DIN70121,
    ISO15118_2,
    ISO15118_20,
};

constexpr auto DIN70121_NAMESPACE = "urn:din:70121:2012:MsgDef";
constexpr auto ISO2_NAMESPACE = "urn:iso:15118:2:2013:MsgDef";
constexpr auto ISO20_DC_PROTOCOL_NAMESPACE = "urn:iso:std:iso:15118:-20:DC";
constexpr auto ISO20_AC_PROTOCOL_NAMESPACE = "urn:iso:std:iso:15118:-20:AC";

// Maps a SupportedAppProtocol namespace string to its ProtocolId, if the namespace is known.
std::optional<ProtocolId> protocol_id_from_namespace(const std::string& protocol_namespace);

} // namespace iso15118
