// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <everest/util/async/monitor.hpp>

#include "config.hpp"
#include <iso15118/d2/config.hpp>
#include <iso15118/d20/config.hpp>
#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/io/connection_abstract.hpp>
#include <iso15118/io/poll_manager.hpp>
#include <iso15118/io/sdp_server.hpp>
#include <iso15118/io/time.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/shared_datatypes.hpp>
#include <iso15118/session/config.hpp>
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

    TbdController(TbdConfig, session::feedback::Callbacks, session::EvseSetupConfig);
    TbdController(TbdConfig, session::feedback::Callbacks, session::EvseSetupConfig, ConnectionFactory);
    ~TbdController();

    // Mutually exclusive with the other driver; see the class note. Refuses (logs and returns) if the other driver is
    // already running.
    void loop();
    // Mutually exclusive with the other driver; see the class note. Refuses (logs and returns false) if the other
    // driver is already running.
    StartSessionResult start_session(int connected_fd, const StartSessionOptions& start_options);
    void tick();

    bool has_active_session() const {
        std::lock_guard<std::mutex> lock(session_mutex);
        return session != nullptr;
    }

    void shutdown();

    void send_control_event(const d20::ControlEvent&);

    void update_authorization_services(const std::vector<message_20::datatypes::Authorization>& services,
                                       bool cert_install_service);
    // ISO 15118-2 Plug-and-Charge per-session setup: whether Contract is offered and whether a locally
    // unverifiable contract chain may be forwarded to the CSMS (central contract validation).
    void update_iso2_pnc_config(bool pnc_enabled, bool central_contract_validation_allowed);
    void update_dc_limits(const d20::DcTransferLimits&);
    void update_powersupply_limits(const d20::DcTransferLimits&);
    // ISO 15118-2: request a (signed) MeteringReceipt from the EV (ReceiptRequired in the charge loop).
    void update_receipt_required(bool);
    void update_energy_modes(const std::vector<message_20::datatypes::ServiceCategory>&);
    // The pre-20 (ISO 15118-2 / DIN SPEC 70121) energy transfer modes, verbatim. Applied to the next
    // session; the -20 service categories above stay the ISO 15118-20 view.
    void update_pre20_energy_transfer_modes(const std::vector<shared_datatypes::EnergyTransferMode>&);
    // The value-added services external providers offer, for the ISO 15118-2 ServiceList (name, scope,
    // free flag per service; the -20 view stays update_supported_vas_services). Applied to the next session.
    void update_pre20_vas_services(const std::vector<session::VasService>&);
    // Narrows the protocol generations offered in the SupportedAppProtocol handshake. Applied to the next
    // session (the handshake reads the session config); priority order is kept.
    void update_supported_protocols(const std::vector<ProtocolId>&);
    void update_ac_limits(const d20::AcTransferLimits&);
    void update_iso2_ac_max_current(float ampere);
    // ISO 15118-2 / DIN SPEC 70121 physical EVSE parameters (EvseManager set_charging_parameters).
    void update_physical_values(const d20::PhysicalValues&);
    // Arm a no-energy pause (IEC 61851-23:2023 CC.3.5.3). Applied to the running session right away and
    // stored for the next session to start, which consumes it.
    void update_no_energy_pause(d20::NoEnergyPauseMode mode);

    void update_supported_vas_services(const std::vector<uint16_t>& vas_services);

    void set_dlink_ready(bool ready);

    void update_supported_der_functions(iec::DERControlName der_control, const iec::DERControlFunction& function);
    void update_unsupported_der_functions(iec::DERControlName der_control);

private:
    io::PollManager poll_manager;
    std::unique_ptr<io::SdpServer> sdp_server;

    // Guards the session pointer. Only the controller loop thread creates, resets and closes the
    // session, but module command threads dereference it to push control events (the event queue
    // itself is thread-safe). Command threads hold this mutex across the null-check-and-call so a
    // concurrent reset() on the loop thread cannot free the Session mid-call.
    mutable std::mutex session_mutex;
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

    everest::lib::util::monitor<session::EvseSetupConfig> evse_setup;

    std::string interface_name;

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    // ISO 15118-2 pause context, owned here so it outlives the per-session D2SeccEngine (mirrors pause_ctx).
    std::optional<d2::PauseContext> d2_pause_ctx{std::nullopt};

    static constexpr uint32_t V2G_COMMUNICATION_SETUP_TIMEOUT_MS{18000};
    // Owned by the loop thread (tick). Module command threads request changes via set_dlink_ready(),
    // which only publishes dlink_ready_requested + bumps dlink_ready_generation; tick applies them.
    std::optional<Timeout> communication_setup_timeout;
    std::atomic_bool dlink_ready_requested{false};
    std::atomic<uint64_t> dlink_ready_generation{0};
    uint64_t dlink_ready_applied{0};

    ConnectionFactory connection_factory;
};

} // namespace iso15118
