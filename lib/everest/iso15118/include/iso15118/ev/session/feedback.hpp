// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <functional>

#include <iso15118/ev/d20/evse_session_info.hpp>
#include <iso15118/io/ipv6_endpoint.hpp>
#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_iec_charge_loop.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/type.hpp>

namespace iso15118::ev::feedback {

struct Callbacks {
    std::function<void(const io::Ipv6EndPoint&)> connected;
    std::function<void(message_20::Type)> v2g_message;
    // Fired when the response watchdog expires (a sent request got no response in
    // time). Distinct from stopped, which fires on every session end.
    std::function<void()> timed_out;
    std::function<void()> stopped;
    std::function<void(const d20::EVSESessionInfo&)> evse_session_info;
    std::function<void()> ev_power_ready;
    std::function<void()> dc_power_on;
    std::function<void()> stop_from_charger;
    std::function<void(const message_20::datatypes::AC_CPDResEnergyTransferMode&)> ac_limits;
    std::function<void(const message_20::datatypes::BPT_AC_CPDResEnergyTransferMode&)> ac_bpt_limits;
    std::function<void(const message_20::datatypes::BPT_DC_CPDResEnergyTransferMode&)> dc_bpt_limits;
    std::function<void(const message_20::datatypes::Dynamic_AC_CLResControlMode&)> ac_target_power;
    std::function<void(const message_20::datatypes::DER_Dynamic_AC_CLResControlMode&)> der_control;
};

} // namespace iso15118::ev::feedback

namespace iso15118::ev {

class Feedback {
public:
    explicit Feedback(feedback::Callbacks);

    void connected(const io::Ipv6EndPoint&) const;
    void v2g_message(message_20::Type) const;
    void timed_out() const;
    void stopped() const;
    void evse_session_info(const d20::EVSESessionInfo&) const;
    void ev_power_ready() const;
    void dc_power_on() const;
    void stop_from_charger() const;
    void ac_limits(const message_20::datatypes::AC_CPDResEnergyTransferMode&) const;
    void ac_bpt_limits(const message_20::datatypes::BPT_AC_CPDResEnergyTransferMode&) const;
    void dc_bpt_limits(const message_20::datatypes::BPT_DC_CPDResEnergyTransferMode&) const;
    void ac_target_power(const message_20::datatypes::Dynamic_AC_CLResControlMode&) const;
    void der_control(const message_20::datatypes::DER_Dynamic_AC_CLResControlMode&) const;

private:
    feedback::Callbacks callbacks;
};

} // namespace iso15118::ev
