// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once
#include "connection_abstract.hpp"

#include <memory>
#include <optional>

#include <iso15118/config.hpp>
#include <iso15118/io/poll_manager.hpp>
#include <iso15118/io/sha_hash.hpp>

namespace iso15118::io {

// forward declaration
struct SSLClientContext;
class ConnectionClientSSL : public IConnection {
public:
    ConnectionClientSSL(PollManager&, const std::string& interface_name, const config::SSLConfig&,
                        const Ipv6EndPoint& secc_endpoint, bool verify_server_certificate);

    void set_event_callback(const ConnectionEventCallback&) final;
    Ipv6EndPoint get_public_endpoint() const final;

    void write(const uint8_t* buf, size_t len) final;
    ReadResult read(uint8_t* buf, size_t len) final;

    void close() final;

    bool is_secure() const final {
        return true;
    }

    std::optional<sha512_hash_t> get_vehicle_cert_hash() const final {
        return std::nullopt;
    }

    ~ConnectionClientSSL();

private:
    PollManager& poll_manager;
    std::unique_ptr<SSLClientContext> ssl;

    Ipv6EndPoint end_point;

    ConnectionEventCallback event_callback{nullptr};

    bool handshake_complete{false};
    bool closed{false};

    bool drive_handshake();
    void handle_data();
};
} // namespace iso15118::io
