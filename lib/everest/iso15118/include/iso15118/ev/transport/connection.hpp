// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
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

#include <iso15118/ev/config/config.hpp>
#include <iso15118/io/ipv6_endpoint.hpp>

namespace iso15118::ev::transport {

// TLS profile for the data path. Tls1_2: cipher ECDHE-ECDSA-AES128-SHA256, no client certificate
// (ISO 15118-2). Tls1_3: the vehicle chain (ISO 15118-20). Auto: the versions allowed below, 1.3
// dropped when the vehicle chain is not usable.
struct TlsParams {
    config::TlsVersion version{config::TlsVersion::Auto};
    // Auto only: the versions the SAP offer has a protocol for.
    bool allow_tls_1_2{true};
    bool allow_tls_1_3{true};
    bool verify_server{true};
    std::string v2g_root_cert_path;
    std::string client_cert_chain_path;
    std::string client_key_path;
    std::string client_key_password;
    bool key_logging{false};
    std::string key_logging_path;
};

// Why the vehicle chain in @p params cannot back a TLS 1.3 mTLS handshake; empty when it can.
std::string vehicle_chain_problem(const TlsParams& params);

/**
 * EV-side data path to the SECC: raw V2GTP bytes over libio tcp_client, or tls_client with
 * \ref TlsParams. Framing lives in \ref iso15118::ev::session::Session.
 * libio connects on a detached thread; the reactor must run.
 */
class Connection {
public:
    /**
     * @brief Construct a client bound to a reactor.
     * @details The libio client is created in \ref connect, where the endpoint is known.
     * @param[in] handler The reactor the client registers with on connect.
     */
    explicit Connection(everest::lib::io::event::fd_event_handler& handler);

    // Unregisters from the reactor: a stale registration would dispatch into freed memory.
    ~Connection();

    // The reactor holds this client's fds and the class stores a reactor
    // reference; a copy or move would leave the reactor pointing at a
    // transferred or destroyed client. Pin the instance.
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;

    /**
     * @brief Connect to @p endpoint over @p device, plain TCP or TLS per @p tls.
     * @details @p on_connected / @p on_failed each fire once per connect. A construction or
     * registration failure fires @p on_failed synchronously.
     * @p device is bound via SO_BINDTODEVICE; it supplies the scope of a link-local address.
     * None of the callbacks registered here or by \ref on_rx / \ref on_closed may destroy this
     * client: they run from members of the object they would free.
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
     * @brief Register the internal client with the reactor this client was constructed on.
     * @details Idempotent: a second call returns true without re-registering. It takes no reactor,
     * because \ref teardown unregisters from the constructor's, and a second one could only disagree.
     * @return True on success, false otherwise.
     */
    bool register_events();

    /**
     * @brief The TLS version the handshake negotiated.
     * @return Empty on a plain TCP connection or before the handshake completed.
     */
    std::optional<config::TlsVersion> negotiated_tls_version();

private:
    // Fire on_failed at most once per connect; guarded by failed_fired.
    void fire_failed();

    // Fire on_closed at most once per connect; guarded by closed_fired.
    void fire_closed();

    // Ready transition of either client. check_cpo runs the [V2G2-875] SECC leaf check first.
    void on_ready(bool check_cpo);

    // The libtls connection of the TLS client, null on plain TCP or before the handshake.
    const ::tls::Connection* tls_connection();

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
