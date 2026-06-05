// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */
#pragma once

#include <array>
#include <cstdint>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/udp/udp_payload.hpp>
#include <functional>
#include <optional>
#include <string>

namespace everest::lib::io::uds {

using uds_payload = udp::udp_payload;

/**
 * @struct uds_info
 * Collection of information needed to send/receive via UDS Datagrams
 */
struct uds_info {
    /** The socket path or abstract name */
    std::string path;
    /** True if the name is in the Linux abstract namespace (starts with \0) */
    bool is_abstract{true};
};

struct uds_fd_payload {
    int fd{-1};
    std::string metadata;
    static constexpr size_t max_size = 256;
};

/**
 * uds_socket_base bundles basic UDS Datagram related functionality.
 * This includes lifetime management, reading, writing and fundamental error checking.
 */
class uds_socket_base {
public:
    uds_socket_base() = default;
    virtual ~uds_socket_base() = default;

    /**
     * @brief Open the socket as a server
     * @param[in] path The local path/name to bind to
     * @param[in] is_abstract Whether to use the Linux abstract namespace
     * @return True on success, false otherwise
     */
    bool open_as_server(std::string const& path, bool is_abstract = true);

    /**
     * @brief Open the socket as a client
     * @param[in] remote The remote server to connect to
     * @param[in] remote_abstract Whether the server uses the abstract namespace
     * @param[in] local (Optional) The local name to bind to, enabling 2-way comms
     * @param[in] local_abstract Whether the local bind uses the abstract namespace
     * @return True on success, false otherwise
     */
    bool open_as_client(std::string const& remote, bool remote_abstract, std::string const& local = "",
                        bool local_abstract = true);

    bool is_open() const;
    void close();
    int get_fd() const;
    int get_error() const;

    bool share_fd_with_remote(uds_fd_payload const& val);
    uds_fd_payload receive_fd_from_remote();

protected:
    bool tx_impl(void const* payload, size_t size, int fd_to_send = -1);
    bool tx_impl(void const* payload, size_t size, uds_info const& destination, int fd_to_send = -1);
    std::optional<uds_info> rx_impl(void* buffer, size_t buffer_size, ssize_t& payload_size,
                                    int* received_fd = nullptr);
    event::unique_fd m_owned_uds_fd;
};

// ============================================================================
// STANDARD DATA SOCKETS (No File Descriptors)
// ============================================================================

class uds_client_socket : public uds_socket_base {
public:
    using PayloadT = uds_payload;

    uds_client_socket() = default;
    ~uds_client_socket() = default;

    bool open(std::string const& remote, bool remote_abstract, std::string const& local = "",
              bool local_abstract = true);
    bool setup(std::string const& remote, bool remote_abstract, std::string const& local, bool local_abstract,
               int timeout_ms);
    void connect(std::function<void(bool, int)> const& setup_cb);

    bool tx(uds_payload const& payload);
    bool rx(uds_payload& payload);

private:
    std::string m_remote;
    bool m_remote_abstract{true};
    std::string m_local;
    bool m_local_abstract{true};
    int m_timeout_ms{0};
    std::array<uint8_t, uds_payload::max_size> rx_buffer;
};

class uds_server_socket : public uds_socket_base {
public:
    using PayloadT = uds_payload;

    uds_server_socket() = default;
    ~uds_server_socket() = default;

    bool open(std::string const& path, bool is_abstract = true);
    bool tx(uds_payload const& payload);
    bool rx(uds_payload& payload);

private:
    std::optional<uds_info> m_last_source;
    std::array<uint8_t, uds_payload::max_size> rx_buffer;
};

// ============================================================================
// FILE DESCRIPTOR SHARING SOCKETS
// ============================================================================

class uds_fd_client_socket : public uds_socket_base {
public:
    using PayloadT = uds_fd_payload;

    uds_fd_client_socket() = default;
    ~uds_fd_client_socket() = default;

    bool open(std::string const& remote, bool remote_abstract, std::string const& local = "",
              bool local_abstract = true);
    bool setup(std::string const& remote, bool remote_abstract, std::string const& local, bool local_abstract,
               int timeout_ms);
    void connect(std::function<void(bool, int)> const& setup_cb);

    bool tx(uds_fd_payload const& payload);
    bool rx(uds_fd_payload& payload);

private:
    std::string m_remote;
    bool m_remote_abstract{true};
    std::string m_local;
    bool m_local_abstract{true};
    int m_timeout_ms{0};
};

class uds_fd_server_socket : public uds_socket_base {
public:
    using PayloadT = uds_fd_payload;

    uds_fd_server_socket() = default;
    ~uds_fd_server_socket() = default;

    bool open(std::string const& path, bool is_abstract = true);
    bool tx(uds_fd_payload const& payload);
    bool rx(uds_fd_payload& payload);

private:
    std::optional<uds_info> m_last_source;
};
} // namespace everest::lib::io::uds
