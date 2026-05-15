// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace iso15118::ev::io {

enum class Security : uint8_t {
    TLS = 0x00,
    NO_TRANSPORT_SECURITY = 0x10,
};

enum class TransportProtocol : uint8_t {
    TCP = 0x00,
    RESERVED_FOR_UDP = 0x10,
};

static constexpr auto SDP_REQ_LENGTH = 10;
static constexpr auto SDP_RES_LENGTH = 20;

class SdpRequest {
public:
    SdpRequest(Security _security, TransportProtocol _protocol) : security(_security), protocol(_protocol) {};
    std::array<uint8_t, SDP_REQ_LENGTH> create_req();

private:
    // V2GTP Header
    const Security security;
    const TransportProtocol protocol;
};

struct SdpResponse {
    explicit SdpResponse(const std::array<uint8_t, SDP_RES_LENGTH>& response_buffer);
    // V2GTP Header
    std::string address;
    uint16_t port;
    Security security;
    TransportProtocol protocol;

    operator bool() const {
        return valid;
    }

private:
    bool valid;
};

} // namespace iso15118::ev::io
