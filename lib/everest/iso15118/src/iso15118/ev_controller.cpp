// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev_controller.hpp>

#include <algorithm>
#include <variant>

#include <iso15118/d20/ev/timeouts.hpp>
#include <iso15118/io/connection_client_plain.hpp>
#include <iso15118/io/connection_client_ssl.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/io/socket_helper.hpp>

namespace iso15118 {

namespace {
using Signal = session::ev::feedback::Signal;
}

EvController::EvController(EvConfig config_, session::ev::feedback::Callbacks callbacks_,
                           session::EvSetupConfig setup_config_) :
    config(std::move(config_)), callbacks(std::move(callbacks_)), setup_config(std::move(setup_config_)) {

    const auto result_interface_check = io::check_and_update_interface(config.interface_name);
    if (result_interface_check) {
        logf_info("Using ethernet interface: %s", config.interface_name.c_str());
    } else {
        throw std::runtime_error("Ethernet interface was not found!");
    }
}

EvController::~EvController() {
    if (sdp_client) {
        poll_manager.unregister_fd(sdp_client->get_fd());
        sdp_client->close();
    }
}

void EvController::loop() {
    static constexpr auto POLL_MANAGER_TIMEOUT_MS = 50;

    shutdown_active.store(false);
    shutdown_signaled = false;

    auto next_event = get_current_time_point();

    while (session or sdp_client or not shutdown_active.load()) {
        const auto poll_timeout_ms = get_timeout_ms_until(next_event, POLL_MANAGER_TIMEOUT_MS);

        try {
            poll_manager.poll(poll_timeout_ms);
        } catch (const std::runtime_error& e) {
            logf_error("Shutdown loop() because of: %s", e.what());
            break;
        }

        next_event = offset_time_point_by_ms(get_current_time_point(), POLL_MANAGER_TIMEOUT_MS);

        drain_commands();

        // Cancel a pending SDP discovery on shutdown instead of waiting for its deadline.
        if (sdp_client and shutdown_active.load()) {
            poll_manager.unregister_fd(sdp_client->get_fd());
            sdp_client->close();
            sdp_client.reset();
            sdp_response.reset();
            charging_active.store(false);
        }

        // A discovered SECC endpoint arrived via SDP: create the session.
        if (sdp_client and sdp_response.has_value()) {
            const auto response = sdp_response.value();

            // Security floor: the SDP response security byte is attacker-controllable, unauthenticated
            // multicast. When TLS is enforced, a response that does not offer TLS must never be able to
            // downgrade us to a plaintext session; ignore it and keep discovering until the overall
            // communication setup timeout is reached.
            if (config.enforce_tls and response.security != io::v2gtp::Security::TLS) {
                logf_warning("Ignoring SDP response without TLS security while enforce_tls is set (possible "
                             "downgrade attempt); keep waiting for a TLS-capable SECC");
                sdp_response.reset();
            } else {
                poll_manager.unregister_fd(sdp_client->get_fd());
                sdp_client->close();
                sdp_client.reset();
                sdp_response.reset();

                const bool secure = (response.security == io::v2gtp::Security::TLS);
                try {
                    create_session(response.endpoint, secure);
                } catch (const std::runtime_error& e) {
                    logf_error("Failed to connect to the discovered SECC: %s", e.what());
                    fail_charging_attempt();
                }
            }
        }

        // SDP retry / overall communication setup timeout.
        if (sdp_client) {
            const auto now = get_current_time_point();
            if (now >= sdp_deadline) {
                logf_warning("SDP discovery timed out (V2G_EVCC_CommunicationSetup_Timeout)");
                poll_manager.unregister_fd(sdp_client->get_fd());
                sdp_client->close();
                sdp_client.reset();
                fail_charging_attempt();
            } else {
                // [V2G2-161, V2G-DC-849] send at most SDP_MAX_REQUESTS requests; stop retransmitting once
                // the cap is reached and let the communication-setup deadline handle the failure.
                if (now >= sdp_next_send and sdp_request_count < d20::ev::SDP_MAX_REQUESTS) {
                    try {
                        sdp_client->send_request(sdp_security);
                        ++sdp_request_count;
                    } catch (const std::runtime_error& e) {
                        logf_error("Failed to send SDP request: %s", e.what());
                    }
                    sdp_next_send = offset_time_point_by_ms(now, d20::ev::SDP_RESEND_INTERVAL_MS);
                }
                if (sdp_request_count < d20::ev::SDP_MAX_REQUESTS) {
                    next_event = std::min(next_event, sdp_next_send);
                }
            }
        }

        // Graceful shutdown of a running session: request a stop and let it wind down cleanly.
        if (session and shutdown_active.load() and not shutdown_signaled) {
            session->push_control_event(d20::ev::StopCharging{true});
            shutdown_signaled = true;
        }

        if (session) {
            try {
                const auto next_session_event = session->poll();
                next_event = std::min(next_event, next_session_event);
            } catch (const std::runtime_error& e) {
                logf_error("Shutting down session because of: %s", e.what());
                session->close();
            }

            if (session->is_finished()) {
                if (session->is_paused()) {
                    const auto sid = session->session_id();
                    const auto proto = session->selected_protocol();
                    if (sid.has_value() and proto.has_value()) {
                        paused_session = PausedSession{sid.value(), proto.value(), sdp_security};
                        logf_info("Session paused; persisted for resume on next start_charging");
                    }
                } else {
                    // A clean termination (or resume that fully completed) clears any paused state.
                    paused_session.reset();
                }
                session.reset();
                charging_active.store(false);
            }
        }
    }

    logf_info("Exiting EvController loop gracefully");
}

void EvController::shutdown() {
    logf_info("Trigger graceful shutdown");
    shutdown_active.store(true);
    poll_manager.abort();
}

bool EvController::start_charging(message_20::datatypes::ServiceCategory energy_service,
                                  message_2::datatypes::EnergyTransferMode iso2_energy_transfer_mode) {
    bool expected = false;
    if (not charging_active.compare_exchange_strong(expected, true)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        pending_start = PendingStart{energy_service, iso2_energy_transfer_mode};
    }
    poll_manager.abort();
    return true;
}

void EvController::stop_charging() {
    queue_event(d20::ev::StopCharging{true});
}

void EvController::pause_charging() {
    queue_event(d20::ev::PauseCharging{true});
}

void EvController::update_dc_parameters(const d20::ev::DcEvChargeParameters& params) {
    std::lock_guard<std::mutex> lock(mutex);
    setup_config.dc_charge_parameters = params;
}

void EvController::set_enforce_iso2_contract(bool enforce) {
    std::lock_guard<std::mutex> lock(mutex);
    setup_config.iso2_pnc.enforce_contract = enforce;
}

void EvController::update_bpt_dc_parameters(const d20::ev::DcEvBptChargeParameters& params) {
    std::lock_guard<std::mutex> lock(mutex);
    setup_config.dc_bpt_charge_parameters = params;
}

void EvController::update_soc(uint8_t soc) {
    queue_event(d20::ev::UpdateSoc{soc});
}

void EvController::update_present_voltage_current(float voltage, float current) {
    queue_event(d20::ev::PresentVoltageCurrent{voltage, current});
}

void EvController::send_control_event(const d20::ev::ControlEvent& event) {
    queue_event(event);
}

void EvController::queue_event(const d20::ev::ControlEvent& event) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        pending_events.push_back(event);
    }
    poll_manager.abort();
}

void EvController::arm_session(const PendingStart& start) {
    bool offers_din{false};
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto session_setup = setup_config;
        session_setup.supported_energy_services = {start.energy_service};
        session_setup.iso2_energy_transfer_mode = start.iso2_energy_transfer_mode;

        // Resume: constrain the SAP offer to the paused protocol and seed its session id.
        if (paused_session.has_value()) {
            session_setup.supported_protocols = {paused_session->protocol};
            session_setup.resumed_session_id = paused_session->session_id;
        } else {
            session_setup.resumed_session_id = std::nullopt;
        }

        // DIN SPEC 70121 is plaintext-only; do not offer it over TLS [V2G-DC-868]. Determine whether this
        // attempt will use TLS (a resumed session keeps its negotiated security).
        const bool secure = paused_session.has_value() ? (paused_session->security == io::v2gtp::Security::TLS)
                                                       : (config.use_tls or config.enforce_tls);
        if (secure) {
            auto& protos = session_setup.supported_protocols;
            protos.erase(std::remove(protos.begin(), protos.end(), ProtocolId::DIN70121), protos.end());
        }

        offers_din = std::find(session_setup.supported_protocols.begin(), session_setup.supported_protocols.end(),
                               ProtocolId::DIN70121) != session_setup.supported_protocols.end();
        armed_session_config = session::EvSessionConfig(std::move(session_setup));
    }

    // Security byte for this attempt: honor a resumed session's negotiated security, otherwise use the
    // configured TLS setting.
    const auto security = paused_session.has_value()
                              ? paused_session->security
                              : ((config.use_tls or config.enforce_tls) ? io::v2gtp::Security::TLS
                                                                        : io::v2gtp::Security::NO_TRANSPORT_SECURITY);

    if (security == io::v2gtp::Security::TLS and offers_din) {
        logf_warning("TLS requested while DIN SPEC 70121 is offered; DIN is plaintext-only per spec");
    }

    // Record the security of this attempt so a paused session persists the real transport (and resumes
    // over it) for both the SDP and the direct-connect path.
    sdp_security = security;

    if (config.enable_sdp) {
        try {
            sdp_client = std::make_unique<io::SdpClient>(config.interface_name);
        } catch (const std::runtime_error& e) {
            logf_error("Failed to create the SDP client: %s", e.what());
            fail_charging_attempt();
            return;
        }
        poll_manager.register_fd(sdp_client->get_fd(), [this]() { this->handle_sdp_input(); });

        const auto now = get_current_time_point();
        sdp_request_count = 0;
        sdp_next_send = now; // send the first request immediately
        sdp_deadline = offset_time_point_by_ms(now, d20::ev::COMMUNICATION_SETUP_TIMEOUT_MS);
        sdp_response.reset();
        return;
    }

    // Direct connection (no SDP): connect straight to the configured SECC endpoint.
    if (not config.direct_secc_endpoint.has_value()) {
        logf_error("enable_sdp is false but no direct_secc_endpoint is configured");
        fail_charging_attempt();
        return;
    }

    try {
        create_session(config.direct_secc_endpoint.value(), security == io::v2gtp::Security::TLS);
    } catch (const std::runtime_error& e) {
        logf_error("Failed to connect to the SECC: %s", e.what());
        fail_charging_attempt();
    }
}

void EvController::create_session(const io::Ipv6EndPoint& endpoint, bool secure) {
    std::unique_ptr<io::IConnection> connection;
    if (secure) {
        connection = std::make_unique<io::ConnectionClientSSL>(poll_manager, config.interface_name, config.ssl,
                                                               endpoint, config.verify_server_certificate);
    } else {
        connection = std::make_unique<io::ConnectionClientPlain>(poll_manager, config.interface_name, endpoint);
    }

    session = std::make_unique<EvSession>(std::move(connection), armed_session_config, callbacks);
}

void EvController::handle_sdp_input() {
    if (not sdp_client) {
        return;
    }
    sdp_response = sdp_client->handle_response();
}

void EvController::drain_commands() {
    std::optional<PendingStart> start;
    std::vector<d20::ev::ControlEvent> events;
    {
        std::lock_guard<std::mutex> lock(mutex);
        start = pending_start;
        pending_start.reset();
        events.swap(pending_events);
    }

    if (start.has_value()) {
        if (not session and not sdp_client) {
            arm_session(start.value());
        } else {
            logf_warning("start_charging ignored: a session attempt is already active");
        }
    }

    for (const auto& event : events) {
        if (session) {
            session->push_control_event(event);
            continue;
        }

        const bool is_stop = std::holds_alternative<d20::ev::StopCharging>(event);
        const bool is_pause = std::holds_alternative<d20::ev::PauseCharging>(event);

        if (is_stop and paused_session.has_value()) {
            // A stop requested while paused would otherwise be dropped (no live session), leaving the
            // stale paused session to be silently resumed by the next start_charging. Clear it instead.
            logf_info("stop_charging while paused; discarding the persisted paused session");
            paused_session.reset();
        }

        if ((is_stop or is_pause) and sdp_client) {
            // A stop/pause requested while SDP discovery is still in flight (no session exists yet)
            // must not be dropped: cancel the pending attempt so no session is established and power
            // delivery started after all. charging_active is released so start_charging can arm anew.
            logf_info("%s during session setup; cancelling the pending SDP discovery",
                      is_stop ? "stop_charging" : "pause_charging");
            poll_manager.unregister_fd(sdp_client->get_fd());
            sdp_client->close();
            sdp_client.reset();
            sdp_response.reset();
            charging_active.store(false);
            // The attempt ended terminally without ever producing a session: report it as finished so
            // waiters on v2g_session_finished (e.g. the car simulator) unblock.
            if (callbacks.v2g_session_finished) {
                callbacks.v2g_session_finished();
            }
        }
    }
}

void EvController::fail_charging_attempt() {
    charging_active.store(false);
    // No EvSession exists on these paths, so EvSession::signal_session_finished_if_done() will never
    // run — give the terminal feedback here instead, exactly like a failed session would: the attempt
    // is finished (unblocks v2g_session_finished waiters) and the data link is down with error.
    if (callbacks.v2g_session_finished) {
        callbacks.v2g_session_finished();
    }
    callbacks.signal(Signal::DLINK_ERROR);
}

} // namespace iso15118
