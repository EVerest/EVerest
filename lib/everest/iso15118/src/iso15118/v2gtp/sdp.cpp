// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/v2gtp/sdp.hpp>

#include <algorithm>

#include <cbv2g/exi_v2gtp.h>

namespace iso15118::v2gtp {

namespace {
bool is_known_security(Security security) {
    switch (security) {
    case Security::TLS:
    case Security::NO_TRANSPORT_SECURITY:
        return true;
    }
    return false;
}
} // namespace

std::vector<uint8_t> encode_sdp_request(const SdpRequest& request) {
    std::vector<uint8_t> buffer(10);
    V2GTP20_WriteHeader(buffer.data(), 2, V2GTP20_SDP_REQUEST_PAYLOAD_ID);
    buffer[8] = static_cast<uint8_t>(request.security);
    buffer[9] = static_cast<uint8_t>(request.transport);
    return buffer;
}

std::optional<SdpRequest> decode_sdp_request(const uint8_t* data, size_t size) {
    if (data == nullptr or size < 10) {
        return std::nullopt;
    }
    uint32_t payload_length = 0;
    if (V2GTP20_ReadHeader(data, &payload_length, V2GTP20_SDP_REQUEST_PAYLOAD_ID) != V2GTP_ERROR__NO_ERROR or
        payload_length != 2 or not is_known_security(static_cast<Security>(data[8])) or
        data[9] != static_cast<uint8_t>(TransportProtocol::TCP)) {
        return std::nullopt;
    }
    return SdpRequest{static_cast<Security>(data[8]), static_cast<TransportProtocol>(data[9])};
}

std::vector<uint8_t> encode_sdp_response(const SdpResponse& response) {
    std::vector<uint8_t> buffer(28);
    V2GTP20_WriteHeader(buffer.data(), 20, V2GTP20_SDP_RESPONSE_PAYLOAD_ID);
    std::copy(response.address.begin(), response.address.end(), buffer.begin() + 8);
    buffer[24] = static_cast<uint8_t>(response.port >> 8);
    buffer[25] = static_cast<uint8_t>(response.port);
    buffer[26] = static_cast<uint8_t>(response.security);
    buffer[27] = static_cast<uint8_t>(response.transport);
    return buffer;
}

std::optional<SdpResponse> decode_sdp_response(const uint8_t* data, size_t size) {
    if (data == nullptr or size < 28) {
        return std::nullopt;
    }
    uint32_t payload_length = 0;
    if (V2GTP20_ReadHeader(data, &payload_length, V2GTP20_SDP_RESPONSE_PAYLOAD_ID) != V2GTP_ERROR__NO_ERROR or
        payload_length != 20 or not is_known_security(static_cast<Security>(data[26])) or
        data[27] != static_cast<uint8_t>(TransportProtocol::TCP)) {
        return std::nullopt;
    }
    SdpResponse response{};
    std::copy_n(data + 8, response.address.size(), response.address.begin());
    response.port = static_cast<uint16_t>((static_cast<uint16_t>(data[24]) << 8) | data[25]);
    response.security = static_cast<Security>(data[26]);
    response.transport = static_cast<TransportProtocol>(data[27]);
    return response;
}

} // namespace iso15118::v2gtp
