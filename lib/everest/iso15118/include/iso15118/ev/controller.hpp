// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/util/async/monitor.hpp>

#include <iso15118/io/ipv6_endpoint.hpp>

#include <iso15118/ev/ac_charge_params.hpp>
#include <iso15118/ev/config.hpp>
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/session.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/ev/transport/data_client.hpp>
#include <iso15118/ev/transport/sdp_client.hpp>

namespace iso15118::ev {

/**
 * One EV charging session attempt: reactor, SdpClient, DataClient, Session. Mirrors
 * \ref iso15118::TbdController. A paused session is handed on via EvConfig::resume.
 * The mutators must not be called after this object is destroyed; the owner clears its pointer first.
 */
class Controller {
public:
    Controller(EvConfig config, feedback::Callbacks callbacks, DcChargeParams initial_dc_params = {},
               AcChargeParams initial_ac_params = {});

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
    void update_present_voltage(float present_voltage);
    void update_present_active_power(float present_active_power);
    // Replace the static DC fields; the live fields keep their current values.
    void update_dc_params(const DcChargeParams& params);

    // After loop(): the paused session to re-join, if the session ended with SessionStop(Pause).
    std::optional<PausedSession> paused_session() const;

private:
    void establish_data_path(const iso15118::io::Ipv6EndPoint& endpoint, iso15118::io::v2gtp::Security security);
    void on_sdp_response(const transport::SdpResponse& response);
    template <typename F> void guarded(const char* op, F&& f);
    // Setup failure: log, DLINK_ERROR, stopped.
    void abort_loop(const char* reason);
    void deliver(const d20::ControlEvent& event);
    void arm_stop_grace();

    EvConfig config;
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
    iso15118::io::v2gtp::Security data_path_security{iso15118::io::v2gtp::Security::NO_TRANSPORT_SECURITY};

    everest::lib::io::event::fd_event_handler reactor;
    std::optional<transport::SdpClient> sdp_client;
    std::unique_ptr<transport::DataClient> data_client;

    // Outlive the Session (its Context references them).
    everest::lib::util::monitor<DcChargeParams> dc_params;
    everest::lib::util::monitor<AcChargeParams> ac_params;

    std::unique_ptr<Session> session;
};

} // namespace iso15118::ev
