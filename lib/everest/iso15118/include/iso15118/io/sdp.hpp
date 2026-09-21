// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

namespace iso15118::io::v2gtp {
enum class Security : uint8_t {
    TLS = 0x00,
    NO_TRANSPORT_SECURITY = 0x10,
};

enum class TransportProtocol : uint8_t {
    TCP = 0x00,
    RESERVED_FOR_UDP = 0x10,
};

static constexpr auto SDP_SERVER_PORT = 15118;

enum class PayloadType : uint16_t {
    SAP = 0x8001,
    Part20Main = 0x8002,
    Part20AC = 0x8003,
    Part20DC = 0x8004,
    Part20DerIec = 0x8010,
    Part20DerSae = 0x8011,
};

constexpr bool is_known_payload_type(PayloadType type) {
    switch (type) {
    case PayloadType::SAP:
    case PayloadType::Part20Main:
    case PayloadType::Part20AC:
    case PayloadType::Part20DC:
    case PayloadType::Part20DerIec:
    case PayloadType::Part20DerSae:
        return true;
    }
    // get_payload_type() casts any uint16_t, so a value outside the enum reaches here.
    return false;
}

} // namespace iso15118::io::v2gtp
