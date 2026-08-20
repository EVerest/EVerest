// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/tls/tls_listener.hpp>
#include <everest/tls/tls.hpp>

#include <cstdint>
#include <string>

// Split out of tls_listener.hpp to keep that header libtls-free.
struct everest::lib::io::tls::tls_listener::Config {
    ::tls::Server::config_t tls{}; // chains, ciphers, verify mode etc.
    std::string bind_addr;
    std::uint16_t bind_port{0}; // 0 selects an ephemeral port
    bool ipv6_only{false};      // when true, AF_INET6 + IPV6_V6ONLY=1
};
