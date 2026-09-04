// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/session/feedback.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118::ev {

Feedback::Feedback(feedback::Callbacks callbacks_) : callbacks(std::move(callbacks_)) {
}

void Feedback::connected(const io::Ipv6EndPoint& endpoint) const {
    call_if_available(callbacks.connected, endpoint);
}

void Feedback::v2g_message(message_20::Type type) const {
    call_if_available(callbacks.v2g_message, type);
}

void Feedback::timed_out() const {
    call_if_available(callbacks.timed_out);
}

void Feedback::stopped() const {
    call_if_available(callbacks.stopped);
}

void Feedback::evse_session_info(const d20::EVSESessionInfo& info) const {
    call_if_available(callbacks.evse_session_info, info);
}

void Feedback::ev_power_ready() const {
    call_if_available(callbacks.ev_power_ready);
}

void Feedback::dc_power_on() const {
    call_if_available(callbacks.dc_power_on);
}

void Feedback::stop_from_charger() const {
    call_if_available(callbacks.stop_from_charger);
}

void Feedback::ac_limits(const message_20::datatypes::AC_CPDResEnergyTransferMode& mode) const {
    call_if_available(callbacks.ac_limits, mode);
}

void Feedback::ac_target_power(const message_20::datatypes::Dynamic_AC_CLResControlMode& mode) const {
    call_if_available(callbacks.ac_target_power, mode);
}

} // namespace iso15118::ev
