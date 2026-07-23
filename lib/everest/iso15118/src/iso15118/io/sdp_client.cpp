// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/io/sdp_client.hpp>

#include <cstring>

#include <unistd.h>

#include <arpa/inet.h>
#include <net/if.h>

#include <cbv2g/exi_v2gtp.h>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/io/socket_helper.hpp>

namespace iso15118::io {

namespace {
constexpr auto LINK_LOCAL_MULTICAST = "ff02::1";
constexpr auto SDP_REQUEST_PAYLOAD_LEN = 2;
constexpr auto SDP_RESPONSE_PAYLOAD_LEN = 20;
} // namespace

SdpClient::SdpClient(const std::string& interface_name_) {
    auto interface_name = interface_name_;

    if (not check_and_update_interface(interface_name)) {
        const auto msg = "Failed to find interface " + interface_name_;
        log_and_throw(msg.c_str());
    }

    fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd == -1) {
        log_and_throw("Failed to open socket");
    }

    // Bind only to specified device
    auto result = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, interface_name.c_str(), interface_name.length());
    if (result == -1) {
        const auto error_msg = adding_err_msg("Setsockopt(SO_BINDTODEVICE) failed");
        log_and_throw(error_msg.c_str());
    }

    interface_index = if_nametoindex(interface_name.c_str());
    if (interface_index == 0) {
        const auto error_msg = adding_err_msg("if_nametoindex failed for " + interface_name);
        log_and_throw(error_msg.c_str());
    }

    result = setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &interface_index, sizeof(interface_index));
    if (result == -1) {
        const auto error_msg = adding_err_msg("Setsockopt(IPV6_MULTICAST_IF) failed");
        log_and_throw(error_msg.c_str());
    }

    destination_address = {AF_INET6, htons(v2gtp::SDP_SERVER_PORT), 0, {}, interface_index};
    if (inet_pton(AF_INET6, LINK_LOCAL_MULTICAST, &destination_address.sin6_addr) <= 0) {
        const auto error_msg = adding_err_msg("Failed to setup SDP multicast address");
        log_and_throw(error_msg.c_str());
    }
}

SdpClient::~SdpClient() {
    if (fd != -1) {
        close();
    }
}

void SdpClient::close() {
    logf_info("Closing Sdp Client");

    const auto close_result = ::close(fd);
    if (close_result == -1) {
        logf_error("Sdp client close() failed");
    } else {
        fd = -1;
    }
}

void SdpClient::send_request(v2gtp::Security security) {
    uint8_t v2g_packet[V2GTP_HEADER_LENGTH + SDP_REQUEST_PAYLOAD_LEN];
    uint8_t* sdp_request = v2g_packet + V2GTP_HEADER_LENGTH;

    sdp_request[0] = static_cast<std::underlying_type_t<v2gtp::Security>>(security);
    sdp_request[1] = static_cast<std::underlying_type_t<v2gtp::TransportProtocol>>(v2gtp::TransportProtocol::TCP);

    V2GTP20_WriteHeader(v2g_packet, SDP_REQUEST_PAYLOAD_LEN, V2GTP20_SDP_REQUEST_PAYLOAD_ID);

    const auto send_result =
        sendto(fd, v2g_packet, sizeof(v2g_packet), 0, reinterpret_cast<const sockaddr*>(&destination_address),
               sizeof(destination_address));
    if (send_result == -1) {
        const auto error_msg = adding_err_msg("Failed to send SDP request");
        log_and_throw(error_msg.c_str());
    }
}

std::optional<SdpResponse> SdpClient::handle_response() {
    const auto read_result = recvfrom(fd, udp_buffer, sizeof(udp_buffer), 0, nullptr, nullptr);
    if (read_result <= 0) {
        logf_warning("Read on sdp client socket failed");
        return std::nullopt;
    }

    if (cmp_equal(read_result, sizeof(udp_buffer))) {
        logf_warning("Read on sdp client socket succeeded, but message is to big for the buffer");
        return std::nullopt;
    }

    uint32_t sdp_payload_len;
    const auto parse_sdp_result = V2GTP20_ReadHeader(udp_buffer, &sdp_payload_len, V2GTP20_SDP_RESPONSE_PAYLOAD_ID);

    if (parse_sdp_result != V2GTP_ERROR__NO_ERROR) {
        logf_warning("Sdp client received an unexpected payload");
        return std::nullopt;
    }

    if (not cmp_equal(read_result, V2GTP_HEADER_LENGTH + SDP_RESPONSE_PAYLOAD_LEN)) {
        logf_warning("Sdp client received a response with an unexpected length");
        return std::nullopt;
    }

    const uint8_t* sdp_response = udp_buffer + V2GTP_HEADER_LENGTH;

    SdpResponse response{};
    memcpy(response.endpoint.address, sdp_response, sizeof(response.endpoint.address));

    uint16_t port;
    memcpy(&port, sdp_response + 16, sizeof(port));
    response.endpoint.port = ntohs(port);

    response.security = static_cast<v2gtp::Security>(sdp_response[18]);
    response.transport_protocol = static_cast<v2gtp::TransportProtocol>(sdp_response[19]);

    return response;
}

} // namespace iso15118::io
