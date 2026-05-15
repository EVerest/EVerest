// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/io/sdp_client.hpp>

#include <arpa/inet.h>
#include <cstring>

#include <cbv2g/exi_v2gtp.h>

namespace iso15118::ev::io {
std::array<uint8_t, SDP_REQ_LENGTH> SdpRequest::create_req() {
    std::array<uint8_t, SDP_REQ_LENGTH> buffer{};

    V2GTP20_WriteHeader(buffer.data(), SDP_REQ_LENGTH, V2GTP20_SDP_REQUEST_PAYLOAD_ID);
    buffer[8] = static_cast<std::underlying_type_t<Security>>(security);
    buffer[9] = static_cast<std::underlying_type_t<TransportProtocol>>(protocol);

    return buffer;
}

SdpResponse::SdpResponse(const std::array<uint8_t, SDP_RES_LENGTH>& response_buffer) : valid(false) {
    uint32_t sdp_payload_len{0};
    const auto parse_sdp_result =
        V2GTP20_ReadHeader(response_buffer.data(), &sdp_payload_len, V2GTP20_SDP_RESPONSE_PAYLOAD_ID);
    if (parse_sdp_result != V2GTP_ERROR__NO_ERROR) {
        // TODO(SL): ADd warning log message
        return;
    }

    address = std::string(response_buffer.begin(), response_buffer.begin() + 16);
    uint16_t port_{};
    std::memcpy(&port_, &response_buffer.at(16), sizeof(port_));
    port = ntohs(port_);
    security = static_cast<Security>(response_buffer.at(18));
    protocol = static_cast<TransportProtocol>(response_buffer.at(19));

    valid = true;
}

} // namespace iso15118::ev::io
