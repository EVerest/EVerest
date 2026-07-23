// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/config.hpp>
#include <iso15118/d20/ev/control_event.hpp>
#include <iso15118/io/ipv6_endpoint.hpp>
#include <iso15118/io/poll_manager.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/io/sdp_client.hpp>
#include <iso15118/io/time.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/session/config.hpp>
#include <iso15118/session/ev.hpp>
#include <iso15118/session/ev_feedback.hpp>
#include <iso15118/session/protocol.hpp>

namespace iso15118 {

struct EvConfig {
    config::SSLConfig ssl;
    std::string interface_name;
    bool enable_sdp{true};
    // Used when enable_sdp == false (tests / direct connection to a known SECC endpoint).
    std::optional<io::Ipv6EndPoint> direct_secc_endpoint{std::nullopt};
    bool use_tls{false};
    // When set, the SECC must offer TLS: an SDP response without TLS security is rejected (no
    // plaintext downgrade). Implies use_tls.
    bool enforce_tls{false};
    bool verify_server_certificate{true};
};

// EVCC-side controller, the client-role mirror of TbdController. It drives a single EvSession at a
// time on a 50 ms poll loop and is re-armable: start_charging arms a session attempt, the session is
// destroyed once finished, and a later start_charging starts a fresh one.
class EvController {
public:
    EvController(EvConfig, session::ev::feedback::Callbacks, session::EvSetupConfig);
    ~EvController();

    // Blocking; runs until shutdown().
    void loop();
    void shutdown();

    // Thread-safe: callable from another thread while loop() runs. Returns true if a session attempt
    // was armed (controller was idle), false if a session/attempt is already active. The
    // iso2_energy_transfer_mode carries the AC/DC granularity used by the ISO 15118-2 and DIN engines
    // (single vs three phase, DC_extended); it is ignored by the ISO 15118-20 path.
    bool start_charging(message_20::datatypes::ServiceCategory energy_service,
                        message_2::datatypes::EnergyTransferMode iso2_energy_transfer_mode =
                            message_2::datatypes::EnergyTransferMode::DC_extended);

    void stop_charging();
    void pause_charging();

    void update_dc_parameters(const d20::ev::DcEvChargeParameters&);

    // Thread-safe. Applies to the next session: select Contract in the ISO 15118-2
    // PaymentServiceSelection even when the SECC does not offer it (negative-testing knob).
    void set_enforce_iso2_contract(bool enforce);
    void update_bpt_dc_parameters(const d20::ev::DcEvBptChargeParameters&);
    void update_soc(uint8_t soc);
    void update_present_voltage_current(float voltage, float current);

    void send_control_event(const d20::ev::ControlEvent&);

private:
    // A pending start request captured from another thread until the loop picks it up.
    struct PendingStart {
        message_20::datatypes::ServiceCategory energy_service;
        message_2::datatypes::EnergyTransferMode iso2_energy_transfer_mode;
    };

    // Persisted state of a paused session, used to re-join it on the next start_charging.
    struct PausedSession {
        std::array<uint8_t, 8> session_id;
        ProtocolId protocol;
        io::v2gtp::Security security;
    };

    void arm_session(const PendingStart& start);
    void create_session(const io::Ipv6EndPoint& endpoint, bool secure);
    void handle_sdp_input();
    void drain_commands();
    // Terminal end of a charging attempt before any EvSession exists (SDP timeout, connect
    // failure, ...): release charging_active and give the same feedback a failed session would
    // give — v2g_session_finished (so waiters like the car simulator unblock) + DLINK_ERROR.
    void fail_charging_attempt();
    void queue_event(const d20::ev::ControlEvent& event);

    EvConfig config;
    session::ev::feedback::Callbacks callbacks;

    io::PollManager poll_manager;
    std::unique_ptr<EvSession> session;

    std::atomic_bool shutdown_active{false};
    std::atomic_bool charging_active{false};
    bool shutdown_signaled{false};

    std::mutex mutex;
    // Guarded by mutex:
    session::EvSetupConfig setup_config;
    std::optional<PendingStart> pending_start{std::nullopt};
    std::vector<d20::ev::ControlEvent> pending_events;

    // Paused session awaiting resume (not shared across threads; only touched from the loop thread).
    std::optional<PausedSession> paused_session{std::nullopt};

    // SDP discovery attempt (enable_sdp == true).
    std::unique_ptr<io::SdpClient> sdp_client;
    std::optional<io::SdpResponse> sdp_response{std::nullopt};
    TimePoint sdp_next_send;
    TimePoint sdp_deadline;
    uint32_t sdp_request_count{0};
    io::v2gtp::Security sdp_security{io::v2gtp::Security::NO_TRANSPORT_SECURITY};

    // Session configuration snapshot built at arm time (used when the SDP response arrives later).
    session::EvSessionConfig armed_session_config;
};

} // namespace iso15118
