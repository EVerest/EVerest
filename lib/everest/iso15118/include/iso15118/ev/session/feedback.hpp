// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <functional>
#include <string>

#include <iso15118/ev/d20/evse_session_info.hpp>
#include <iso15118/io/ipv6_endpoint.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/protocol.hpp>

namespace iso15118::ev::feedback {

enum class Signal {
    DLINK_TERMINATE, // session ended, link may be torn down
    DLINK_PAUSE,     // session paused, keep the link for a resume
    DLINK_ERROR,     // no session was established (SDP timeout, connect/handshake failure) or teardown
};

// SECC limits from the DC charge parameter discovery, in the SECC-side shape.
using DcMaximumLimits = session::feedback::DcMaximumLimits;

struct Callbacks {
    std::function<void(const io::Ipv6EndPoint&)> connected;
    std::function<void(Signal)> signal;
    std::function<void(ProtocolId)> selected_protocol;
    std::function<void(const std::string&)> evse_id;
    std::function<void(const DcMaximumLimits&)> dc_evse_present_limits;
    std::function<void()> pause_from_charger;
    // Fired when the response watchdog expires (a sent request got no response in
    // time). Distinct from stopped, which fires on every session end.
    std::function<void()> timed_out;
    std::function<void()> stopped;
    std::function<void(const d20::EVSESessionInfo&)> evse_session_info;
    std::function<void()> ev_power_ready;
    std::function<void()> dc_power_on;
    std::function<void()> stop_from_charger;
};

} // namespace iso15118::ev::feedback

namespace iso15118::ev {

class Feedback {
public:
    explicit Feedback(feedback::Callbacks);

    void connected(const io::Ipv6EndPoint&) const;
    void signal(feedback::Signal) const;
    void selected_protocol(ProtocolId) const;
    void evse_id(const std::string&) const;
    void dc_evse_present_limits(const feedback::DcMaximumLimits&) const;
    void pause_from_charger() const;
    void timed_out() const;
    void stopped() const;
    void evse_session_info(const d20::EVSESessionInfo&) const;
    void ev_power_ready() const;
    void dc_power_on() const;
    void stop_from_charger() const;

private:
    feedback::Callbacks callbacks;
};

} // namespace iso15118::ev
