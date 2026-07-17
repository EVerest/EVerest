// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/tbd_controller.hpp>

#include <algorithm>
#include <chrono>

#include <iso15118/io/connection_plain.hpp>
#include <iso15118/io/connection_ssl.hpp>
#include <iso15118/session/iso.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/io/socket_helper.hpp>

namespace iso15118 {

static constexpr auto POLL_MANAGER_TIMEOUT_MS = 50;

TbdController::TbdController(TbdConfig config_, session::feedback::Callbacks callbacks_, d20::EvseSetupConfig setup_) :
    TbdController(std::move(config_), std::move(callbacks_), std::move(setup_),
                  [](io::PollManager& poll_manager_, const std::string& interface_name_) {
                      return std::make_unique<io::ConnectionPlain>(poll_manager_, interface_name_);
                  }) {
}

TbdController::TbdController(TbdConfig config_, session::feedback::Callbacks callbacks_, d20::EvseSetupConfig setup_,
                             ConnectionFactory connection_factory_) :
    config(std::move(config_)),
    callbacks(std::move(callbacks_)),
    evse_setup(std::move(setup_)),
    interface_name(config.interface_name),
    connection_factory(std::move(connection_factory_)) {

    const auto result_interface_check = io::check_and_update_interface(interface_name);
    if (result_interface_check) {
        logf_info("Using ethernet interface: %s", interface_name.c_str());
    } else {
        throw std::runtime_error("Ethernet interface was not found!");
    }

    if (config.enable_sdp_server) {
        sdp_server = std::make_unique<io::SdpServer>(interface_name);
        poll_manager.register_fd(sdp_server->get_fd(), [this]() { handle_sdp_server_input(); });
    }
}

TbdController::~TbdController() {
    if (config.enable_sdp_server) {
        poll_manager.unregister_fd(sdp_server->get_fd());
        sdp_server->close();
    }
}

void TbdController::loop() {
    shutdown_active.store(false);
    shutdown_signaled = false;

    next_event = get_current_time_point();

    while (session or not shutdown_active.load()) {
        const auto poll_timeout_ms = get_timeout_ms_until(next_event, POLL_MANAGER_TIMEOUT_MS);

        try {
            poll_manager.poll(poll_timeout_ms);
        } catch (const std::runtime_error& e) {
            logf_error("Shutdown loop() because of: %s", e.what());
            break;
        }

        tick();
    }
    logf_info("Exiting TbdController loop gracefully");
}

void TbdController::tick() {
    next_event = offset_time_point_by_ms(get_current_time_point(), POLL_MANAGER_TIMEOUT_MS);

    if (communication_setup_timeout && communication_setup_timeout->is_reached()) {
        logf_warning("V2G communication setup timeout (18s) expired before session was established");
        communication_setup_timeout.reset();
        if (sdp_server) {
            sdp_server->set_dlink_ready(false);
        }
        callbacks.signal(session::feedback::Signal::DLINK_ERROR);
    }

    if (session and shutdown_active.load() and not shutdown_signaled) {
        session->request_shutdown(); // Stopping the session
        shutdown_signaled = true;
    }

    // Consume the flag unconditionally so a request raised while no session is
    // active cannot tear down a later session.
    const bool terminate = terminate_session_requested.exchange(false);
    if (session and terminate) {
        logf_info("Data link lost; terminating active V2G session");
        session->close();
    }

    if (session) {
        try {
            const auto next_session_event = session->poll();
            next_event = std::min(next_event, next_session_event);
        } catch (const std::runtime_error& e) {
            logf_error("Shutting down session because of: %s", e.what());
            logf_info("Restarting session ...");
            session->close();
        }

        if (session->is_finished()) {
            session.reset();
        }
    }

    if (not session and not shutdown_active.load() and not config.enable_sdp_server) {
        session = std::make_unique<Session>(connection_factory(poll_manager, interface_name),
                                            d20::SessionConfig(*evse_setup.handle()), callbacks, pause_ctx);
    }
}

void TbdController::shutdown() {
    logf_info("Trigger graceful shutdown");
    shutdown_active.store(true);
}

void TbdController::send_control_event(const d20::ControlEvent& event) {
    if (session) {
        session->push_control_event(event);
    }
}

void TbdController::update_authorization_services(const std::vector<message_20::datatypes::Authorization>& services,
                                                  bool cert_install_service) {

    {
        auto s = evse_setup.handle();
        s->enable_certificate_install_service = cert_install_service;

        if (services.empty()) {
            logf_warning("The authorization services are not updated because services are empty!");
            return;
        }
        s->authorization_services = services;
    }
}

void TbdController::update_dc_limits(const d20::DcTransferLimits& limits) {

    {
        auto s = evse_setup.handle();
        s->dc_limits = limits;
    }

    if (session) {
        session->push_control_event(limits);
    }
}

void TbdController::update_powersupply_limits(const d20::DcTransferLimits& limits) {
    auto s = evse_setup.handle();
    s->powersupply_limits = limits;
}

void TbdController::update_energy_modes(const std::vector<message_20::datatypes::ServiceCategory>& modes) {
    {
        auto s = evse_setup.handle();
        s->supported_energy_services = modes;
    }

    if (session) {
        session->push_control_event(modes);
    }
}

void TbdController::update_supported_vas_services(const d20::SupportedVASs& vas_services) {

    {
        auto s = evse_setup.handle();
        s->supported_vas_services = vas_services;
    }

    if (session) {
        session->push_control_event(vas_services);
    }
}

void TbdController::update_ac_limits(const d20::AcTransferLimits& limits) {

    {
        auto s = evse_setup.handle();
        s->ac_limits = limits;
    }

    if (session) {
        session->push_control_event(limits);
    }
}

void TbdController::set_dlink_ready(bool ready) {
    if (sdp_server) {
        sdp_server->set_dlink_ready(ready);
    }

    if (ready) {
        communication_setup_timeout.emplace(V2G_COMMUNICATION_SETUP_TIMEOUT_MS);
        logf_info("V2G communication setup timeout started (%u ms)", V2G_COMMUNICATION_SETUP_TIMEOUT_MS);
    } else {
        communication_setup_timeout.reset();
        terminate_session_requested.store(true);
    }
}

void TbdController::update_supported_der_functions(iec::DERControlName der_control,
                                                   const iec::DERControlFunction& function) {
    auto s = evse_setup.handle();
    auto& der_setup = s->der_setup_config.has_value() ? s->der_setup_config.value() : s->der_setup_config.emplace();

    der_setup.supported_der_control_functions[der_control] = function;
}

void TbdController::update_unsupported_der_functions(iec::DERControlName der_control) {
    auto s = evse_setup.handle();
    if (s->der_setup_config.has_value()) {
        logf_info("Removing supported DER control function: %u", static_cast<uint32_t>(der_control));
        auto& der_setup = s->der_setup_config.value();
        der_setup.supported_der_control_functions.erase(der_control);
    }
}

void TbdController::handle_sdp_server_input() {
    auto request = sdp_server->get_peer_request();

    if (not sdp_server->is_dlink_ready()) {
        logf_info("Ignoring SDP request because dlink is not ready");
        return;
    }

    if (shutdown_active.load()) {
        logf_warning("Ignoring sdp request message because the TbdController loop is being shutdown");
        return;
    }

    if (session) {
        // A reconnect SDP arriving in the same poll cycle as a pending teardown
        // is dropped here; the EVCC retransmits its SDP request (~250 ms) and
        // recovers.
        logf_warning("Ignoring sdp request message because a session is already created and running");
        return;
    }

    if (not request) {
        return;
    }

    switch (config.tls_negotiation_strategy) {
    case config::TlsNegotiationStrategy::ACCEPT_CLIENT_OFFER:
        // nothing to change
        break;
    case config::TlsNegotiationStrategy::ENFORCE_TLS:
        request.security = io::v2gtp::Security::TLS;
        break;
    case config::TlsNegotiationStrategy::ENFORCE_NO_TLS:
        request.security = io::v2gtp::Security::NO_TRANSPORT_SECURITY;
        break;
    }

    auto connection = [this](bool secure_connection) -> std::unique_ptr<io::IConnection> {
        try {
            if (secure_connection) {
                return std::make_unique<io::ConnectionSSL>(poll_manager, interface_name, config.ssl);
            }
            return std::make_unique<io::ConnectionPlain>(poll_manager, interface_name);
        } catch (const std::runtime_error& e) {
            logf_error("%s", e.what());
            return nullptr;
        }
    }(request.security == io::v2gtp::Security::TLS);

    if (not connection) {
        logf_error("A TCP/TLS connection could not be established. Ignoring this SDP request for now");
        return;
    }

    const auto ipv6_endpoint = connection->get_public_endpoint();

    session = std::make_unique<Session>(std::move(connection), d20::SessionConfig(*evse_setup.handle()), callbacks,
                                        pause_ctx);
    communication_setup_timeout.reset();

    // Deliberately do not clear terminate_session_requested here. A data-link
    // loss that races this session creation must win: tick() consumes the flag
    // and reaps whatever session exists, fresh or not. The EVCC retransmits its
    // SDP request (~250 ms) so a session torn down this way recovers, whereas
    // swallowing the flag would strand a session on a dead link.
    sdp_server->send_response(request, ipv6_endpoint);
}

} // namespace iso15118
