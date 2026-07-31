// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <netinet/in.h>

#include "ipv6_endpoint.hpp"
#include "sdp.hpp"

namespace iso15118::io {

struct SdpResponse {
    Ipv6EndPoint endpoint;
    v2gtp::Security security;
    v2gtp::TransportProtocol transport_protocol;
};

class SdpClient {
public:
    explicit SdpClient(const std::string& interface_name);
    ~SdpClient();

    void close();
    void send_request(v2gtp::Security security);
    std::optional<SdpResponse> handle_response();

    auto get_fd() const {
        return fd;
    }

private:
    int fd{-1};
    unsigned int interface_index{0};
    struct sockaddr_in6 destination_address {};
    uint8_t udp_buffer[2048];
};

} // namespace iso15118::io
