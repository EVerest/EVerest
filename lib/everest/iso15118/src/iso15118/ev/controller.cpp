// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/controller.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include <everest/io/event/timer_fd.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/io/socket_helper.hpp>
#include <iso15118/ev/config/validation.hpp>
#include <iso15118/ev/d2/timeouts.hpp>
#include <iso15118/ev/d20/timeouts.hpp>
#include <iso15118/ev/detail/session/session.hpp>
#include <iso15118/ev/detail/session_options.hpp>
#include <iso15118/ev/session/sap_offer.hpp>
#include <iso15118/ev/transport/connection.hpp>
#include <iso15118/ev/transport/sdp_client.hpp>

namespace iso15118::ev {

namespace {

// Bounds SDP discovery + connect (+ TLS handshake). Matches the SECC's V2G_COMMUNICATION_SETUP_TIMEOUT_MS.
constexpr auto SETUP_TIMEOUT = std::chrono::milliseconds(18000);

// Minimum grace for the graceful stop walk; configured bounds may require longer.
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

} // namespace

std::chrono::milliseconds stop_grace(const config::EvConfig& config) {
    // Charge-loop response, PowerDelivery(Stop), final welding response, -20 Finished, and SessionStop.
    const auto response = std::max(config.response_timeout, d2::timeouts::MESSAGE_POWER_DELIVERY);
    const auto send = std::max(config.send_delay, std::chrono::milliseconds(100));
    return std::max(STOP_GRACE, config.welding_detection.timeout + 5 * response + 4 * send);
}

d20::SessionOptions make_session_options(const config::EvConfig& config, std::vector<session::OfferedProtocol> offer) {
    d20::SessionOptions options;
    options.d20 = config.d20;
    options.has_cp_state_feedback = config.has_cp_state_feedback;
    options.welding_detection = config.welding_detection;
    if (config.resume.has_value()) {
        options.resume =
            d20::Resume{config.resume->session_id, config.resume->secc_clock, config.resume->selected_service};
    }
    options.offered_protocols = std::move(offer);
    options.energy_request = config.params.energy_request;
    return options;
}

bool resolve_interface(std::string& interface_name) {
    return io::check_and_update_interface(interface_name);
}

Controller::~Controller() = default;

const std::string& Controller::interface_name() const {
    return config.interface_name;
}

Controller::Controller(config::EvConfig config_, feedback::Callbacks callbacks_,
                       params::DcChargeParams initial_dc_params, params::AcChargeParams initial_ac_params) :
    config(std::move(config_)),
    feedback(callbacks_),
    dc_params(std::move(initial_dc_params)),
    ac_params(std::move(initial_ac_params)) {

    if (not resolve_interface(config.interface_name)) {
        throw std::runtime_error("Ethernet interface was not found: " + config.interface_name);
    }

    if (config.enable_sdp) {
        sdp_client = std::make_unique<transport::SdpClient>(config.interface_name, config.sdp_security());
    } else if (not config.direct_secc_endpoint.has_value()) {
        throw std::runtime_error("EV Controller: enable_sdp is false but no direct_secc_endpoint is configured");
    }

    // One V2G root: -2 CertificateInstallation reads the TLS one.
    config.params.v2g_root_cert_path = config.tls.v2g_root_cert_path;

    auto offer = session::offer_for_config(config);
    if (offer.empty()) {
        throw std::runtime_error("EV Controller: nothing to offer in SupportedAppProtocolReq");
    }
    std::vector<message_20::SupportedAppProtocol> advertised;
    for (const auto& o : offer) {
        advertised.push_back(o.entry);
    }

    session.reset(new session::Session(
        callbacks_,
        [this](std::vector<uint8_t> frame) {
            if (not connection) {
                logf_error("EV Controller: outbound frame before connection exists; dropping");
                return false;
            }
            if (not connection->send(frame)) {
                logf_error("EV Controller: connection failed to send outbound frame");
                return false;
            }
            return true;
        },
        reactor, session::SessionTiming{config.send_delay, config.response_timeout}, config.evcc_id,
        std::move(advertised), &dc_params, &ac_params, config.energy_service,
        make_session_options(config, std::move(offer)), config.params));

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

void Controller::on_sdp_response(const iso15118::v2gtp::SdpResponse& response) {
    if (config.tls.mode == config::TlsMode::Require and response.security != iso15118::v2gtp::Security::TLS) {
        logf_warning("EV Controller: SECC offers no TLS but tls is require; ignoring the SDP response");
        return;
    }
    sdp_retry.disarm();
    iso15118::io::Ipv6EndPoint endpoint{};
    std::memcpy(endpoint.address, response.address.data(), response.address.size());
    endpoint.port = response.port;
    establish_data_path(endpoint, response.security);
}

void Controller::establish_data_path(const iso15118::io::Ipv6EndPoint& endpoint, iso15118::v2gtp::Security security) {
    // UDP may duplicate the SECC reply; never rebuild an in-flight data path.
    if (connection) {
        logf_warning("EV Controller: ignoring additional SDP response; data path already established");
        return;
    }

    guarded("establishing the data path", [&]() {
        feedback.connected(endpoint);

        // The SECC's security byte decides the transport: a TLS SECC only accepts TLS.
        const bool use_tls = security == iso15118::v2gtp::Security::TLS;
        data_path_security = security;
        // The Session exists before SDP has run, so the engines learn the transport here rather
        // than from a constructor argument. This is the first point at which it is known and it is
        // before any engine reads it.
        session->set_transport_security(security);
        std::optional<transport::TlsParams> tls;
        if (use_tls) {
            tls.emplace();
            tls->version = config.tls.version;
            tls->verify_server = config.tls.verify_server_certificate;
            tls->v2g_root_cert_path = config.tls.v2g_root_cert_path;
            tls->client_cert_chain_path = config.tls.client_cert_chain_path;
            tls->client_key_path = config.tls.client_key_path;
            tls->client_key_password = config.tls.client_key_password;
            tls->key_logging = config.tls.enable_key_logging;
            tls->key_logging_path = config.tls.key_logging_path;
            if (config.tls.version == config::TlsVersion::Auto) {
                const auto versions = session::auto_tls_versions(config);
                tls->allow_tls_1_2 = versions.tls_1_2;
                tls->allow_tls_1_3 = versions.tls_1_3;
                const auto problem =
                    config::auto_tls_problem(config, versions.tls_1_2 ? "" : transport::vehicle_chain_problem(*tls));
                if (not problem.empty()) {
                    logf_error("EV Controller: %s; stopping", problem.c_str());
                    online = false;
                    return;
                }
            }
        }

        connection = std::make_unique<transport::Connection>(reactor);
        connection->on_rx([this](const std::vector<uint8_t>& bytes) { session->on_bytes_received(bytes); });
        connection->on_closed([this]() { session->on_peer_closed(); });

        connection->connect(
            endpoint, config.interface_name, tls,
            [this]() {
                setup_timeout.disarm();
                sdp_retry.disarm();
                if (not narrow_offer_to_negotiated_tls()) {
                    online = false;
                    return;
                }
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
                    logf_error("EV Controller: connect failed; stopping");
                }
                online = false;
            });
    });
}

bool Controller::narrow_offer_to_negotiated_tls() {
    const auto negotiated = connection->negotiated_tls_version();
    if (config.tls.version != config::TlsVersion::Auto or not negotiated.has_value()) {
        return true;
    }
    auto offer = session::offer_for_config(config, negotiated);
    if (offer.empty()) {
        std::string enabled;
        for (const auto protocol : config.supported_protocols) {
            enabled += (enabled.empty() ? "" : ", ") + std::string(protocol_id_to_string(protocol));
        }
        logf_error("EV Controller: TLS %s negotiated but none of the enabled protocols (%s) runs over it; closing",
                   negotiated == config::TlsVersion::Tls1_3 ? "1.3" : "1.2", enabled.c_str());
        return false;
    }
    session->set_offer(std::move(offer));
    return true;
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
        sdp_client->discover([this](const iso15118::v2gtp::SdpResponse& response) { on_sdp_response(response); });
    } else {
        if (config.tls.mode == config::TlsMode::Require and config.direct_security != iso15118::v2gtp::Security::TLS) {
            abort_loop("tls is require but the configured direct endpoint offers no transport security");
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
    const auto grace = stop_grace(config);
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
    {
        auto h = dc_params.handle();
        (*h).present_voltage = present_voltage;
    }
    auto h = ac_params.handle();
    (*h).present_voltage = present_voltage;
}

void Controller::update_present_active_power(float present_active_power) {
    auto h = ac_params.handle();
    (*h).present_active_power = present_active_power;
}

void Controller::update_present_frequency(float present_frequency) {
    auto h = ac_params.handle();
    (*h).present_frequency = present_frequency;
}

void Controller::update_der_alarm_status(std::uint32_t der_alarm_status) {
    auto h = ac_params.handle();
    (*h).der_alarm_status = der_alarm_status;
}

void Controller::update_dc_params(const params::DcChargeParams& params) {
    auto h = dc_params.handle();
    const auto soc = (*h).present_soc;
    const auto voltage = (*h).present_voltage;
    *h = params;
    (*h).present_soc = soc;
    (*h).present_voltage = voltage;
}

std::optional<config::PausedSession> Controller::paused_session() const {
    if (not session or not session->is_paused()) {
        return std::nullopt;
    }
    const auto id = session->session_id();
    const auto protocol = session->selected_protocol();
    if (not id.has_value() or not protocol.has_value()) {
        return std::nullopt;
    }
    return config::PausedSession{id.value(), protocol.value(), data_path_security, session->secc_clock_state(),
                                 session->service_selection()};
}

} // namespace iso15118::ev
