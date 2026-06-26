// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tcp/tcp_client.hpp>

#include <iso15118/io/ipv6_endpoint.hpp>

namespace iso15118::ev::io {

/**
 * EV-side TCP data-path client.
 *
 * Wraps a libio \ref everest::lib::io::tcp::tcp_client to carry the raw V2GTP
 * byte stream to the SECC after \ref SdpClient has discovered its endpoint.
 * The connection is established asynchronously by libio (a detached thread),
 * so the owning reactor must be pumped for the on-connected callback and the
 * I/O to fire.
 *
 * This class exposes the raw bytes only; V2GTP framing lives in
 * \ref iso15118::ev::Session, keeping the TLS swap seam clean (a future TLS
 * client can drop in behind the same interface).
 */
class DataClient {
public:
    /**
     * @brief Construct a client bound to a reactor.
     * @details The target endpoint is only known at \ref connect time, so the
     * underlying libio client is created there; this constructor only stores
     * the reactor reference used for registration.
     * @param[in] handler The reactor the client registers with on connect.
     */
    explicit DataClient(everest::lib::io::event::fd_event_handler& handler);

    // The reactor holds this client's fds and the class stores a reactor
    // reference; a copy or move would leave the reactor pointing at a
    // transferred or destroyed client. Pin the instance.
    DataClient(const DataClient&) = delete;
    DataClient& operator=(const DataClient&) = delete;
    DataClient(DataClient&&) = delete;
    DataClient& operator=(DataClient&&) = delete;

    /**
     * @brief Connect to the SECC data endpoint.
     * @details Constructs the underlying libio TCP client targeting @p endpoint
     * and registers it with the reactor. The connection runs asynchronously;
     * @p on_connected is invoked once when the client becomes ready, while
     * @p on_failed is invoked once on a connect or socket failure. Both
     * one-shot latches are reset on every @ref connect, so each call gets a
     * fresh single fire of whichever outcome occurs.
     *
     * A second @ref connect tears down any prior registration first, so a
     * reconnect re-registers the fresh client with the reactor.
     *
     * @note Construction or validation failures (address formatting, TCP client
     * construction, or registration) fire @p on_failed synchronously, before
     * @ref connect returns, on the caller's stack. Asynchronous connect/socket
     * failures fire @p on_failed later from the reactor thread.
     * @param[in] endpoint The SECC TCP endpoint (address + port, wire bytes).
     * @param[in] on_connected Callback fired once per connect on ready.
     * @param[in] on_failed Callback fired once per connect on failure.
     */
    void connect(const iso15118::io::Ipv6EndPoint& endpoint, std::function<void()> on_connected,
                 std::function<void()> on_failed);

    /**
     * @brief Send a raw frame to the SECC.
     * @details Forwards to the libio client's tx, which buffers a copy and
     * transmits it once the socket is writable.
     * @param[in] frame The bytes to transmit.
     * @return False if called before \ref connect or the client is on error,
     * true otherwise.
     */
    bool send(const std::vector<uint8_t>& frame);

    /**
     * @brief Register a callback for received bytes.
     * @details May be called before \ref connect; the callback is stored and
     * invoked by the client's rx handler with each chunk of received data.
     * @param[in] handler The callback used as RX handler.
     */
    void on_rx(std::function<void(const std::vector<uint8_t>&)> handler);

    /**
     * @brief Register the internal TCP client with an event handler.
     * @details Idempotent: a second call returns true without re-registering.
     * The tcp_client registers via the fd-keyed fd_event_sync_interface
     * overload (which would itself reject a duplicate fd), but the guard also
     * avoids needlessly re-running the registration setup.
     * @param[in] handler The reactor to register with.
     * @return True on success, false otherwise.
     */
    bool register_events(everest::lib::io::event::fd_event_handler& handler);

private:
    // Fire on_failed at most once per connect; latched by failed_fired.
    void fire_failed();

    everest::lib::io::event::fd_event_handler& handler;
    bool registered{false};
    bool connected_fired{false};
    bool failed_fired{false};
    std::function<void()> on_connected;
    std::function<void()> on_failed;
    std::function<void(const std::vector<uint8_t>&)> m_on_rx;
    std::unique_ptr<everest::lib::io::tcp::tcp_client> client;
};

} // namespace iso15118::ev::io
