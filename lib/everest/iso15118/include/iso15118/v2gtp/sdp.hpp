// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace iso15118::v2gtp {
enum class Security : uint8_t {
    TLS = 0x00,
    NO_TRANSPORT_SECURITY = 0x10,
};

enum class TransportProtocol : uint8_t {
    TCP = 0x00,
    RESERVED_FOR_UDP = 0x10,
};

inline constexpr auto SDP_SERVER_PORT = 15118;

// [V2G2-138] V2G_UDP_SDP_CLIENT, ISO 15118-2 Table 15: the SDP client's source port range.
inline constexpr std::uint16_t SDP_CLIENT_PORT_MIN = 49152;
inline constexpr std::uint16_t SDP_CLIENT_PORT_MAX = 65535;

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

struct SdpRequest {
    Security security;
    TransportProtocol transport;
};

struct SdpResponse {
    std::array<uint8_t, 16> address;
    uint16_t port; // host byte order
    Security security;
    TransportProtocol transport;
};

std::vector<uint8_t> encode_sdp_request(const SdpRequest&);
std::optional<SdpRequest> decode_sdp_request(const uint8_t*, size_t);
std::vector<uint8_t> encode_sdp_response(const SdpResponse&);
std::optional<SdpResponse> decode_sdp_response(const uint8_t*, size_t);

} // namespace iso15118::v2gtp
