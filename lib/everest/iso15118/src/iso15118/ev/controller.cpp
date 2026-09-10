// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/controller.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include <everest/io/event/timer_fd.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/io/socket_helper.hpp>
#include <iso15118/ev/d20/timeouts.hpp>
#include <iso15118/ev/sap_offer.hpp>

namespace iso15118::ev {

namespace {

// Bounds SDP discovery + connect (+ TLS handshake). Matches the SECC's V2G_COMMUNICATION_SETUP_TIMEOUT_MS.
constexpr auto SETUP_TIMEOUT = std::chrono::milliseconds(18000);

// Worst-case graceful stop walk: PowerDelivery(Stop) -> DC_WeldingDetection -> SessionStop.
constexpr auto STOP_GRACE = std::chrono::milliseconds(3 * 2000 + 60000);

template <typename F> class ScopeExit {
public:
    explicit ScopeExit(F f) : m_f(std::move(f)) {
    }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ~ScopeExit() {
        m_f();
    }

private:
    F m_f;
};

d20::SessionOptions make_session_options(const EvConfig& config, std::vector<OfferedProtocol> offer) {
    d20::SessionOptions options;
    options.control_mode = config.control_mode;
    options.supported_auth_options = config.supported_auth_options;
    options.has_cp_state_feedback = config.has_cp_state_feedback;
    if (config.resume.has_value()) {
        options.resumed_session_id = config.resume->session_id;
    }
    options.offered_protocols = std::move(offer);
    return options;
}

std::vector<OfferedProtocol> make_offer(const EvConfig& config) {
    if (not config.advertised_app_protocols.empty()) {
        return offer_from_app_protocols(config.advertised_app_protocols);
    }
    SapOfferInput input;
    input.supported_protocols = config.supported_protocols;
    input.energy_service = config.energy_service;
    input.tls = config.tls.use_tls or config.tls.enforce_tls;
    input.custom_protocol = config.custom_protocol;
    if (config.resume.has_value()) {
        input.resume_protocol = config.resume->protocol;
    }
    return build_sap_offer(input);
}

} // namespace

Controller::Controller(EvConfig config_, feedback::Callbacks callbacks_, DcChargeParams initial_dc_params,
                       AcChargeParams initial_ac_params) :
    config(std::move(config_)),
    feedback(callbacks_),
    dc_params(std::move(initial_dc_params)),
    ac_params(std::move(initial_ac_params)) {

    if (not io::check_and_update_interface(config.interface_name)) {
        throw std::runtime_error("Ethernet interface was not found: " + config.interface_name);
    }

    if (config.enable_sdp) {
        sdp_client.emplace(config.interface_name, config.sdp_security());
    } else if (not config.direct_secc_endpoint.has_value()) {
        throw std::runtime_error("EV Controller: enable_sdp is false but no direct_secc_endpoint is configured");
    }

    auto offer = make_offer(config);
    if (offer.empty()) {
        throw std::runtime_error("EV Controller: nothing to offer in SupportedAppProtocolReq");
    }
    std::vector<message_20::SupportedAppProtocol> advertised;
    for (const auto& o : offer) {
        advertised.push_back(o.entry);
    }

    session = std::make_unique<Session>(
        callbacks_,
        [this](std::vector<uint8_t> frame) {
            if (not data_client) {
                logf_error("EV Controller: outbound frame before data client exists; dropping");
                return false;
            }
            if (not data_client->send(frame)) {
                logf_error("EV Controller: data client failed to send outbound frame");
                return false;
            }
            return true;
        },
        reactor, SessionTiming{config.send_delay, config.response_timeout}, config.evcc_id, std::move(advertised),
        &dc_params, &ac_params, config.energy_service, config.der_control_functions,
        config.der_stop_on_unsupported_functions, make_session_options(config, std::move(offer)), config.params);

    session->set_on_finished([this]() {
        online = false;
        stop_grace_timer.disarm();
    });
}

template <typename F> void Controller::guarded(const char* op, F&& f) {
    try {
        f();
    } catch (const std::exception& e) {
        logf_error("EV Controller: %s failed (%s); stopping", op, e.what());
        online = false;
    } catch (...) {
        logf_error("EV Controller: %s failed (non-std exception); stopping", op);
        online = false;
    }
}

void Controller::abort_loop(const char* reason) {
    logf_error("EV Controller: %s; aborting", reason);
    feedback.signal(feedback::Signal::DLINK_ERROR);
    feedback.stopped();
}

void Controller::on_sdp_response(const transport::SdpResponse& response) {
    if (config.tls.enforce_tls and response.security != iso15118::io::v2gtp::Security::TLS) {
        logf_warning("EV Controller: SECC offers no TLS but enforce_tls is set; ignoring the SDP response");
        return;
    }
    sdp_retry.disarm();
    establish_data_path(response.endpoint, response.security);
}

void Controller::establish_data_path(const iso15118::io::Ipv6EndPoint& endpoint,
                                     iso15118::io::v2gtp::Security security) {
    // UDP may duplicate the SECC reply; never rebuild an in-flight data path.
    if (data_client) {
        logf_warning("EV Controller: ignoring additional SDP response; data path already established");
        return;
    }

    guarded("establishing the data path", [&]() {
        feedback.connected(endpoint);

        // The SECC's security byte decides the transport: a TLS SECC only accepts TLS.
        const bool use_tls = security == iso15118::io::v2gtp::Security::TLS;
        data_path_security = security;
        std::optional<transport::TlsParams> tls;
        if (use_tls) {
            tls = transport::TlsParams{config.tls.enable_tls_1_3,     config.tls.verify_server_certificate,
                                       config.tls.v2g_root_cert_path, config.tls.client_cert_chain_path,
                                       config.tls.client_key_path,    config.tls.client_key_password,
                                       config.tls.enable_key_logging, config.tls.key_logging_path};
        }

        data_client = std::make_unique<transport::DataClient>(reactor);
        data_client->on_rx([this](const std::vector<uint8_t>& bytes) { session->on_bytes_received(bytes); });
        data_client->on_closed([this]() { session->on_peer_closed(); });

        data_client->connect(
            endpoint, config.interface_name, tls,
            [this]() {
                setup_timeout.disarm();
                sdp_retry.disarm();
                session_started = true;
                session->start();
            },
            [this]() {
                if (session_started) {
                    // The link dropped mid-session: let the Session tear down so DLINK_TERMINATE
                    // and on_finished still fire.
                    logf_error("EV Controller: data path lost; stopping");
                    session->on_peer_closed();
                } else {
                    logf_error("EV Controller: data client connect failed; stopping");
                }
                online = false;
            });
    });
}

void Controller::loop() {
    online = true;

    if (stop_requested) {
        online = false;
        feedback.stopped();
        return;
    }

    ScopeExit unregister_timers{[this]() {
        reactor.unregister_event_handler(&setup_timeout);
        reactor.unregister_event_handler(&sdp_retry);
        reactor.unregister_event_handler(&stop_grace_timer);
    }};

    setup_timeout.set_single_shot(true);
    if (not reactor.register_event_handler(&setup_timeout, [this]() {
            logf_warning("EV Controller: SDP discovery / connect did not complete in time");
            sdp_retry.disarm();
            online = false;
        })) {
        abort_loop("failed to register the setup timeout timer");
        return;
    }
    if (not setup_timeout.set_timeout(SETUP_TIMEOUT)) {
        abort_loop("failed to arm the setup timeout timer");
        return;
    }

    stop_grace_timer.set_single_shot(true);
    if (not reactor.register_event_handler(&stop_grace_timer, [this]() {
            logf_warning("EV Controller: graceful stop did not finish in time; hard-stopping");
            online = false;
        })) {
        abort_loop("failed to register the stop-grace timer");
        return;
    }

    if (config.enable_sdp) {
        if (not sdp_client->register_events(reactor)) {
            abort_loop("failed to register the SDP client");
            return;
        }

        sdp_retry.set_single_shot(false);
        if (not reactor.register_event_handler(&sdp_retry, [this]() {
                if (not sdp_client) {
                    return;
                }
                if (sdp_requests_sent >= d20::timeouts::SDP_MAX_REQUESTS) {
                    logf_warning("EV Controller: SDP_max_request (%u) reached without a usable response",
                                 d20::timeouts::SDP_MAX_REQUESTS);
                    sdp_retry.disarm();
                    online = false;
                    return;
                }
                ++sdp_requests_sent;
                sdp_client->send_request();
            })) {
            abort_loop("failed to register the SDP retry timer");
            return;
        }
        if (not sdp_retry.set_timeout(d20::timeouts::SDP_RESEND_INTERVAL)) {
            abort_loop("failed to arm the SDP retry timer");
            return;
        }

        sdp_requests_sent = 1;
        sdp_client->discover([this](transport::SdpResponse response) { on_sdp_response(response); });
    } else {
        if (config.tls.enforce_tls and config.direct_security != iso15118::io::v2gtp::Security::TLS) {
            abort_loop("enforce_tls is set but the configured direct endpoint offers no transport security");
            return;
        }
        establish_data_path(config.direct_secc_endpoint.value(), config.direct_security);
    }

    reactor.run(online);

    if (session and session_started and not session->is_finished()) {
        logf_warning("EV Controller: session did not finish before the deadline");
    }
    if (not session_started) {
        feedback.signal(feedback::Signal::DLINK_ERROR);
    }

    feedback.stopped();
}

void Controller::arm_stop_grace() {
    // Never shorter than the stop walk itself: a small response_timeout must not hard-stop a
    // legitimate PowerDelivery(Stop) -> DC_WeldingDetection -> SessionStop sequence.
    const auto grace =
        (config.response_timeout.count() > 0) ? std::max(3 * config.response_timeout, STOP_GRACE) : STOP_GRACE;
    if (not stop_grace_timer.set_timeout(grace)) {
        logf_error("EV Controller: failed to arm the stop-grace timer; hard-stopping");
        online = false;
    }
}

void Controller::deliver(const d20::ControlEvent& event) {
    reactor.add_action([this, event]() {
        if (session) {
            session->deliver_control_event(event);
        }
    });
}

void Controller::request_stop() {
    reactor.add_action([this]() {
        if (session) {
            session->deliver_control_event(d20::ControlEvent{d20::StopCharging{true}});
        }
        arm_stop_grace();
    });
}

void Controller::request_pause() {
    reactor.add_action([this]() {
        if (session) {
            session->deliver_control_event(d20::ControlEvent{d20::PauseCharging{true}});
        }
        arm_stop_grace();
    });
}

void Controller::terminate() {
    reactor.add_action([this]() {
        if (session and session_started) {
            session->terminate();
        } else {
            logf_warning("EV Controller: terminating the charging attempt before a session exists");
            sdp_retry.disarm();
            online = false;
        }
    });
}

void Controller::shutdown() {
    stop_requested = true;
    online = false;
    reactor.add_action([]() {});
}

void Controller::set_cp_state(bool c_or_d) {
    deliver(d20::ControlEvent{d20::CpState{c_or_d}});
}

void Controller::update_present_soc(double present_soc) {
    auto h = dc_params.handle();
    (*h).present_soc = present_soc;
}

void Controller::update_present_voltage(float present_voltage) {
    auto h = dc_params.handle();
    (*h).present_voltage = present_voltage;
}

void Controller::update_present_active_power(float present_active_power) {
    auto h = ac_params.handle();
    (*h).present_active_power = present_active_power;
}

void Controller::update_dc_params(const DcChargeParams& params) {
    auto h = dc_params.handle();
    const auto soc = (*h).present_soc;
    const auto voltage = (*h).present_voltage;
    *h = params;
    (*h).present_soc = soc;
    (*h).present_voltage = voltage;
}

std::optional<PausedSession> Controller::paused_session() const {
    if (not session or not session->is_paused()) {
        return std::nullopt;
    }
    const auto id = session->session_id();
    const auto protocol = session->selected_protocol();
    if (not id.has_value() or not protocol.has_value()) {
        return std::nullopt;
    }
    return PausedSession{id.value(), protocol.value(), data_path_security};
}

} // namespace iso15118::ev
