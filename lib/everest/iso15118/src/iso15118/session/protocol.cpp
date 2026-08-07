// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/protocol.hpp>

namespace iso15118 {

std::optional<ProtocolId> protocol_id_from_namespace(const std::string& protocol_namespace) {
    if (protocol_namespace == ISO20_DC_PROTOCOL_NAMESPACE or protocol_namespace == ISO20_AC_PROTOCOL_NAMESPACE) {
        return ProtocolId::ISO15118_20;
    }
    if (protocol_namespace == ISO2_NAMESPACE) {
        return ProtocolId::ISO15118_2;
    }
    if (protocol_namespace == DIN70121_NAMESPACE) {
        return ProtocolId::DIN70121;
    }
    return std::nullopt;
}

} // namespace iso15118
