// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/ev_feedback.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118::session::ev {

Feedback::Feedback(feedback::Callbacks callbacks_) : callbacks(std::move(callbacks_)) {
}

void Feedback::signal(feedback::Signal signal) const {
    call_if_available(callbacks.signal, signal);
}

void Feedback::ev_power_ready(bool ready) const {
    call_if_available(callbacks.ev_power_ready, ready);
}

void Feedback::dc_power_on() const {
    if (callbacks.dc_power_on) {
        callbacks.dc_power_on();
    }
}

void Feedback::ac_evse_target_power(const d20::AcTargetPower& target_power) const {
    call_if_available(callbacks.ac_evse_target_power, target_power);
}

void Feedback::stop_from_charger() const {
    if (callbacks.stop_from_charger) {
        callbacks.stop_from_charger();
    }
}

void Feedback::pause_from_charger() const {
    if (callbacks.pause_from_charger) {
        callbacks.pause_from_charger();
    }
}

void Feedback::v2g_session_finished() const {
    if (callbacks.v2g_session_finished) {
        callbacks.v2g_session_finished();
    }
}

void Feedback::selected_protocol(const std::string& protocol) const {
    call_if_available(callbacks.selected_protocol, protocol);
}

void Feedback::evse_id(const std::string& evse_id) const {
    call_if_available(callbacks.evse_id, evse_id);
}

void Feedback::dc_evse_present_limits(const feedback::DcMaximumLimits& limits) const {
    call_if_available(callbacks.dc_evse_present_limits, limits);
}

void Feedback::v2g_message(const V2gMessageType& type) const {
    call_if_available(callbacks.v2g_message, type);
}

} // namespace iso15118::session::ev
