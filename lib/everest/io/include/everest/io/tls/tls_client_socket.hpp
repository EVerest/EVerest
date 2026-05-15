// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tcp/tcp_socket.hpp>
#include <everest/tls/tls.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace everest::lib::io::tls {

/**
 * @brief TLS client-side connection policy for fd_event_client.
 *
 * Wraps a tls::Client and exposes the fd_event_client ClientPolicy interface:
 * async TCP connect + TLS handshake via setup()/connect(), non-blocking tx/rx,
 * and fd accessors.  A synchronous path is also available via open().
 *
 * The TLS configuration is supplied as the first argument to setup() / open()
 * so that consumers using the event-loop-driven alias
 * `event::fd_event_client<tls_client_socket>::type` can configure the policy
 * at client-construction time (the fd_event_client constructor forwards its
 * arguments to setup()).
 */
class tls_client_socket {
public:
    /** @brief Payload type for tx/rx operations. */
    using PayloadT = std::vector<std::uint8_t>;

    /** @brief Configuration bundle for TLS and SNI. */
    struct Config {
        ::tls::Client::config_t tls{};
        std::string host_for_sni; //!< hostname sent in the TLS SNI extension (usually = remote host)
    };

    tls_client_socket() = default;
    tls_client_socket(tls_client_socket const&) = delete;
    tls_client_socket(tls_client_socket&&) = default;
    tls_client_socket& operator=(tls_client_socket const&) = delete;
    tls_client_socket& operator=(tls_client_socket&&) = default;
    ~tls_client_socket() = default;

    /**
     * @brief Synchronous open: TCP connect followed by TLS handshake.
     *
     * Blocks until the full TLS handshake completes or the timeout is reached.
     *
     * @param cfg TLS and SNI configuration.
     * @param remote_host Remote host address or hostname.
     * @param remote_port Remote TCP port.
     * @return true on success, false otherwise.
     */
    bool open(Config cfg, std::string const& remote_host, std::uint16_t remote_port);

    /**
     * @brief Prepare for an asynchronous connection.
     *
     * Builds the SSL_CTX and starts a non-blocking TCP connect. Returns
     * quickly. Must be followed by a call to connect().
     *
     * @param cfg TLS and SNI configuration.
     * @param remote_host Remote host address or hostname.
     * @param remote_port Remote TCP port.
     * @param timeout_ms TCP connect timeout in milliseconds.
     * @return true on success, false otherwise.
     */
    bool setup(Config cfg, std::string const& remote_host, std::uint16_t remote_port, int timeout_ms);

    /**
     * @brief Complete the async connection (TCP + TLS handshake).
     *
     * May block until the connection succeeds or fails.  Intended to be
     * invoked on a worker thread by fd_event_client.  Calls cb(true, fd) on
     * success and cb(false, -1) on failure.
     *
     * @param cb Completion callback: cb(ok, fd).
     */
    void connect(std::function<void(bool, int)> const& cb);

    /**
     * @brief Send bytes over the TLS connection.
     *
     * On a partial write the sent prefix is erased from payload and false is
     * returned; the caller should re-arm and retry. Returns true only when
     * payload has been fully consumed.
     *
     * @param payload Bytes to send. Modified in place on partial writes.
     * @return true on complete send, false on partial send, want_*, or error.
     */
    bool tx(PayloadT& payload);

    /**
     * @brief Receive bytes from the TLS connection.
     *
     * Replaces buffer contents with newly received data (fd_event_client
     * Policy contract). Returns false on want_* (no data yet).
     *
     * @param buffer Output buffer; cleared and overwritten on success.
     * @return true when data was received, false on want_* or error.
     */
    bool rx(PayloadT& buffer);

    /**
     * @brief Get the underlying socket file descriptor.
     * @return The TLS connection fd when the handshake is done; the TCP fd
     *         otherwise; -1 if neither is available.
     */
    int get_fd() const;

    /**
     * @brief Get the last recorded error code.
     * @return errno-style value; 0 if no error.
     */
    int get_error() const;

    /**
     * @brief Query whether a live TLS connection is held.
     * @return true while the internal ConnectionPtr is non-null.
     */
    bool is_open() const;

    /**
     * @brief Shut down and release the TLS connection.
     *
     * Issues a TLS shutdown, resets the connection pointer, and closes
     * the underlying TCP socket.
     */
    void close();

    /**
     * @brief Query which poll event the policy currently wants monitored.
     *
     * After construction and on want_read this is poll_events::read.
     * After a want_write result this transitions to poll_events::write.
     *
     * @return The event to arm before the next call to tx() or rx().
     */
    event::poll_events desired_events() const;

private:
    /**
     * @brief Drive the TLS handshake to completion within a timeout.
     *
     * Wraps tcp_fd via wrap_connecting_fd() and calls the blocking
     * ClientConnection::connect(timeout_ms) to complete the handshake.
     *
     * @param tcp_fd Raw TCP file descriptor to wrap.
     * @param timeout_ms Handshake timeout in milliseconds (passed to connect()).
     * @return true on success, false on failure.
     */
    bool finish_handshake(int tcp_fd, int timeout_ms);

    Config m_cfg{};
    tcp::tcp_socket m_tcp{};
    std::unique_ptr<::tls::Client> m_client{};
    ::tls::Client::ConnectionPtr m_conn{};
    event::poll_events m_desired{event::poll_events::read};
    int m_last_error{0};
    bool m_handshake_done{false};
};

} // namespace everest::lib::io::tls
