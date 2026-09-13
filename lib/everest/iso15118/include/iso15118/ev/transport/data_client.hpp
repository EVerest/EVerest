// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/fd_event_sync_interface.hpp>
#include <everest/io/tcp/tcp_client.hpp>
#include <everest/io/tls/tls_client.hpp>

#include <iso15118/io/ipv6_endpoint.hpp>

namespace iso15118::ev::transport {

// TLS profile for the data path. tls_1_3 false: TLS 1.2, cipher ECDHE-ECDSA-AES128-SHA256, no client
// certificate (ISO 15118-2). tls_1_3 true: TLS 1.3 with the vehicle chain (ISO 15118-20).
struct TlsParams {
    bool tls_1_3{false};
    bool verify_server{true};
    std::string v2g_root_cert_path;
    std::string client_cert_chain_path;
    std::string client_key_path;
    std::string client_key_password;
    bool key_logging{false};
    std::string key_logging_path;
};

/**
 * EV-side data path to the SECC: raw V2GTP bytes over libio tcp_client, or tls_client with
 * \ref TlsParams. Framing lives in \ref iso15118::ev::Session.
 * libio connects on a detached thread; the reactor must run.
 */
class DataClient {
public:
    /**
     * @brief Construct a client bound to a reactor.
     * @details The libio client is created in \ref connect, where the endpoint is known.
     * @param[in] handler The reactor the client registers with on connect.
     */
    explicit DataClient(everest::lib::io::event::fd_event_handler& handler);

    // Unregisters from the reactor: a stale registration would dispatch into freed memory.
    ~DataClient();

    // The reactor holds this client's fds and the class stores a reactor
    // reference; a copy or move would leave the reactor pointing at a
    // transferred or destroyed client. Pin the instance.
    DataClient(const DataClient&) = delete;
    DataClient& operator=(const DataClient&) = delete;
    DataClient(DataClient&&) = delete;
    DataClient& operator=(DataClient&&) = delete;

    /**
     * @brief Connect to @p endpoint over @p device, plain TCP or TLS per @p tls.
     * @details @p on_connected / @p on_failed each fire once per connect. A construction or
     * registration failure fires @p on_failed synchronously.
     * @p device is bound via SO_BINDTODEVICE; it supplies the scope of a link-local address.
     */
    void connect(const iso15118::io::Ipv6EndPoint& endpoint, const std::string& device,
                 const std::optional<TlsParams>& tls, std::function<void()> on_connected,
                 std::function<void()> on_failed);

    /**
     * @brief Register a callback for the peer closing the connection (EOF).
     * @details Fired at most once per connect, on a plain-TCP EOF or a TLS close_notify.
     * A peer close fires this instead of on_failed, which stays reserved for real errors.
     */
    void on_closed(std::function<void()> handler);

    /**
     * @brief Send a raw frame to the SECC.
     * @details Buffered by libio and transmitted once the socket is writable.
     * @param[in] frame The bytes to transmit.
     * @return False before \ref connect or on a client error, true otherwise.
     */
    bool send(const std::vector<uint8_t>& frame);

    /**
     * @brief Register a callback for received bytes.
     * @details May be called before \ref connect; it runs per received chunk.
     * @param[in] handler The callback used as RX handler.
     */
    void on_rx(std::function<void(const std::vector<uint8_t>&)> handler);

    /**
     * @brief Register the internal TCP client with an event handler.
     * @details Idempotent: a second call returns true without re-registering.
     * @param[in] handler The reactor to register with.
     * @return True on success, false otherwise.
     */
    bool register_events(everest::lib::io::event::fd_event_handler& handler);

private:
    // Fire on_failed at most once per connect; guarded by failed_fired.
    void fire_failed();

    // Fire on_closed at most once per connect; guarded by closed_fired.
    void fire_closed();

    // Ready transition of either client. check_cpo runs the [V2G2-875] SECC leaf check first.
    void on_ready(bool check_cpo);

    // True when the TLS peer leaf carries a DomainComponent=="CPO" RDN [V2G2-875].
    bool peer_is_cpo();

    // Unregister and drop whichever client exists.
    void teardown();

    // Queue teardown() on the reactor: a client must not be destroyed from a callback the reactor
    // is dispatching, which is where the [V2G2-875] rejection runs.
    void defer_teardown();

    // The registered client, plain or TLS, null before connect.
    everest::lib::io::event::fd_event_sync_interface* active_client();

    everest::lib::io::event::fd_event_handler& handler;
    bool registered{false};
    bool connected_fired{false};
    bool failed_fired{false};
    bool closed_fired{false};
    std::function<void()> on_connected;
    std::function<void()> on_failed;
    std::function<void()> m_on_closed;
    std::function<void(const std::vector<uint8_t>&)> m_on_rx;
    std::unique_ptr<everest::lib::io::tcp::tcp_client> client;
    std::unique_ptr<everest::lib::io::tls::tls_client> tls_client;
    // Lifetime token for the deferred teardown: the reactor may run the queued action after this
    // object is gone, and the destructor cannot cancel it.
    std::shared_ptr<int> life{std::make_shared<int>(0)};
};

} // namespace iso15118::ev::transport
