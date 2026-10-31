// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/util/async/monitor.hpp>

#include <iso15118/ev/config/config.hpp>
#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/feedback.hpp>
#include <iso15118/ev/params/ac.hpp>
#include <iso15118/ev/params/dc.hpp>

namespace iso15118::ev {

namespace session {
class Session;
} // namespace session

namespace transport {
class Connection;
class SdpClient;
} // namespace transport

// Resolve "auto" to an IPv6 interface and validate the resulting name.
bool resolve_interface(std::string& interface_name);

/**
 * One EV charging session attempt: reactor, SdpClient, Connection, Session.
 * A paused session is handed on to the next Controller via config::EvConfig::resume.
 * The mutators must not be called after this object is destroyed; the owner clears its pointer first.
 */
class Controller {
public:
    Controller(config::EvConfig config, feedback::Callbacks callbacks, params::DcChargeParams initial_dc_params = {},
               params::AcChargeParams initial_ac_params = {});
    ~Controller();

    const std::string& interface_name() const;

    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(Controller&&) = delete;

    /**
     * @brief Discover the SECC (SDP, or the configured direct endpoint), connect, run the reactor.
     * @details Returns on session end, setup deadline, SDP_max_request, a failed connect, or
     * shutdown()/terminate(); fires feedback.stopped before returning.
     */
    void loop();

    // Graceful EV-initiated stop (StopCharging), bounded by a grace timer.
    void request_stop();

    // Graceful EV-initiated pause (PauseCharging): SessionStop(Pause); paused_session() afterwards.
    void request_pause();

    // Immediate teardown without SessionStop (CP state E/F, unplug); also cancels discovery/connect.
    void terminate();

    // Stop the loop: flag + reactor wake. Valid before, during and after loop().
    void shutdown();

    // Control pilot state applied by the EV (true: C or D). Latched and delivered as a CpState event.
    void set_cp_state(bool c_or_d);

    // Module -> FSM parameter channels (any thread).
    void update_present_soc(double present_soc);
    // V. DC inlet voltage, and the AC measured grid voltage for AC_DER_SAE.
    void update_present_voltage(float present_voltage);
    void update_present_active_power(float present_active_power);
    // Hz. AC measured grid frequency for AC_DER_SAE.
    void update_present_frequency(float present_frequency);
    // DERAlarmStatus bitmap, AMD1 Table M.9, for AC_DER_SAE.
    void update_der_alarm_status(std::uint32_t der_alarm_status);
    // Replace the static DC fields; the live fields keep their current values.
    void update_dc_params(const params::DcChargeParams& params);

    // After loop(): the paused session to re-join, if the session ended with SessionStop(Pause).
    std::optional<config::PausedSession> paused_session() const;

private:
    void establish_data_path(const iso15118::io::Ipv6EndPoint& endpoint, iso15118::v2gtp::Security security);
    void on_sdp_response(const iso15118::v2gtp::SdpResponse& response);
    // Under tls_version auto, rebuild the SAP offer for the negotiated TLS version. False, logged, when
    // nothing enabled runs over it.
    bool narrow_offer_to_negotiated_tls();
    template <typename F> void guarded(const char* op, F&& f);
    // Setup failure: log, DLINK_ERROR, stopped.
    void abort_loop(const char* reason);
    void deliver(const d20::ControlEvent& event);
    void arm_stop_grace();

    config::EvConfig config;
    const Feedback feedback;

    std::atomic_bool online{false};
    std::atomic_bool stop_requested{false};

    // Pre-session phase (SDP + connect + handshake); the session's watchdogs take over after start().
    everest::lib::io::event::timer_fd setup_timeout;
    everest::lib::io::event::timer_fd sdp_retry;
    uint32_t sdp_requests_sent{0};
    // Graceful stop/pause fallback: hard-stops the loop when the stop walk does not finish.
    everest::lib::io::event::timer_fd stop_grace_timer;

    // Set once a Session ran; a loop that ends without one reports DLINK_ERROR.
    bool session_started{false};
    // Transport security of the data path, recorded for paused_session().
    iso15118::v2gtp::Security data_path_security{iso15118::v2gtp::Security::NO_TRANSPORT_SECURITY};

    everest::lib::io::event::fd_event_handler reactor;
    std::unique_ptr<transport::SdpClient> sdp_client;
    std::unique_ptr<transport::Connection> connection;

    // Outlive the Session (its Context references them).
    everest::lib::util::monitor<params::DcChargeParams> dc_params;
    everest::lib::util::monitor<params::AcChargeParams> ac_params;

    struct SessionDeleter {
        void operator()(session::Session*) const noexcept;
    };
    std::unique_ptr<session::Session, SessionDeleter> session;
};

} // namespace iso15118::ev
