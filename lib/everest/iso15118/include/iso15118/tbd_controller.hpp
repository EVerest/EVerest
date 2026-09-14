// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <everest/util/async/monitor.hpp>

#include "config.hpp"
#include <iso15118/d20/config.hpp>
#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/io/connection_abstract.hpp>
#include <iso15118/io/poll_manager.hpp>
#include <iso15118/io/sdp_server.hpp>
#include <iso15118/io/time.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/iso.hpp>

namespace iso15118 {

enum class StartSessionResult : std::uint8_t {
    KeepFdOpen,
    FdNotValid,
    FdClosed,
    SessionComplete,
};

struct StartSessionOptions {
    std::optional<io::sha512_hash_t> vehicle_cert_hash{std::nullopt};
    bool skip_app_protocol_negotiation{false};
};

struct TbdConfig {
    /// `ssl` here is the INITIAL value only; the live config is owned by `m_ssl_config` after construction —
    /// read it via `ssl_config_snapshot()`, never `config.ssl`.
    config::SSLConfig ssl{};
    std::string interface_name;
    config::TlsNegotiationStrategy tls_negotiation_strategy{config::TlsNegotiationStrategy::ACCEPT_CLIENT_OFFER};
    bool enable_sdp_server{true};
};

// TbdController is single-threaded. Exactly one driver may run at a time: either loop() or start_session(), driven from
// a single thread. They must never run concurrently - they share the poll manager and session state, which are not
// thread-safe. Sequential reuse (one driver returns, then the other is called) is allowed.
class TbdController {
public:
    // Creates the connection for sessions when the SDP server is disabled.
    using ConnectionFactory =
        std::function<std::unique_ptr<io::IConnection>(io::PollManager&, const std::string& interface_name)>;

    TbdController(TbdConfig, session::feedback::Callbacks, d20::EvseSetupConfig);
    TbdController(TbdConfig, session::feedback::Callbacks, d20::EvseSetupConfig, ConnectionFactory);
    ~TbdController();

    // Mutually exclusive with the other driver; see the class note. Refuses (logs and returns) if the other driver is
    // already running.
    void loop();
    // Mutually exclusive with the other driver; see the class note. Refuses (logs and returns false) if the other
    // driver is already running.
    StartSessionResult start_session(int connected_fd, const StartSessionOptions& start_options);
    void tick();

    bool has_active_session() const {
        return session != nullptr;
    }

    void shutdown();

    void send_control_event(const d20::ControlEvent&);

    void update_authorization_services(const std::vector<message_20::datatypes::Authorization>& services,
                                       bool cert_install_service);
    void update_dc_limits(const d20::DcTransferLimits&);
    void update_powersupply_limits(const d20::DcTransferLimits&);
    void update_energy_modes(const std::vector<message_20::datatypes::ServiceCategory>&);
    void update_ac_limits(const d20::AcTransferLimits&);

    void update_supported_vas_services(const std::vector<uint16_t>& vas_services);

    void set_dlink_ready(bool ready);

    void update_supported_der_functions(iec::DERControlName der_control, const iec::DERControlFunction& function);
    void update_unsupported_der_functions(iec::DERControlName der_control);

    /// Replaces the stored SSL config (thread-safe). Takes effect for the next incoming secure connection;
    /// established connections are unaffected.
    void set_ssl_config(config::SSLConfig new_config);

    /// Returns a copy of the stored SSL config (thread-safe).
    [[nodiscard]] config::SSLConfig ssl_config_snapshot() const;

    /// The single seam every new secure connection reads its SSL config from, so a rotation via
    /// `set_ssl_config()` is picked up by the next connection.
    [[nodiscard]] config::SSLConfig connection_ssl_config() const;

private:
    io::PollManager poll_manager;
    std::unique_ptr<io::SdpServer> sdp_server;

    std::unique_ptr<Session> session;

    std::atomic_bool shutdown_active{false};
    std::atomic_bool terminate_session_requested{false};

    // Guards against loop() and start_session() running concurrently (see class note).
    std::atomic_bool driver_running{false};

    bool shutdown_signaled{false};
    TimePoint next_event{};

    // callbacks for sdp server
    void handle_sdp_server_input();

    // Runs one poll_manager.poll() step using next_event; returns false if poll()
    // threw, so the caller can break out of its loop.
    bool poll_once();

    // Services the currently active session for one cycle: propagates a pending
    // shutdown, consumes the terminate request, polls the session and reaps it when
    // finished. Shared by loop()/tick() and start_session().
    void service_active_session();

    const TbdConfig config;
    const session::feedback::Callbacks callbacks;

    everest::lib::util::monitor<d20::EvseSetupConfig> evse_setup;

    std::string interface_name;

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};

    static constexpr uint32_t V2G_COMMUNICATION_SETUP_TIMEOUT_MS{18000};
    std::optional<Timeout> communication_setup_timeout;

    ConnectionFactory connection_factory;

    mutable everest::lib::util::monitor<config::SSLConfig> m_ssl_config;
};

} // namespace iso15118
