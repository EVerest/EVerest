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

void Feedback::v2g_message(const V2gMessageType& type) const {
    call_if_available(callbacks.v2g_message, type);
}

void Feedback::signal(feedback::Signal signal) const {
    call_if_available(callbacks.signal, signal);
}

void Feedback::selected_protocol(ProtocolId protocol) const {
    call_if_available(callbacks.selected_protocol, protocol);
}

void Feedback::evse_id(const std::string& id) const {
    call_if_available(callbacks.evse_id, id);
}

void Feedback::dc_evse_present_limits(const feedback::DcMaximumLimits& limits) const {
    call_if_available(callbacks.dc_evse_present_limits, limits);
}

void Feedback::pause_from_charger() const {
    call_if_available(callbacks.pause_from_charger);
}

void Feedback::pnc_contract_installed(const std::string& contract_chain_pem, const std::string& contract_key_pem,
                                      const std::string& emaid) const {
    call_if_available(callbacks.pnc_contract_installed, contract_chain_pem, contract_key_pem, emaid);
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

void Feedback::ac_bpt_limits(const message_20::datatypes::BPT_AC_CPDResEnergyTransferMode& mode) const {
    call_if_available(callbacks.ac_bpt_limits, mode);
}

void Feedback::dc_bpt_limits(const message_20::datatypes::BPT_DC_CPDResEnergyTransferMode& mode) const {
    call_if_available(callbacks.dc_bpt_limits, mode);
}

void Feedback::ac_target_power(const iso15118::d20::AcTargetPower& target) const {
    call_if_available(callbacks.ac_target_power, target);
}

void Feedback::der_control(const message_20::datatypes::DER_Dynamic_AC_CLResControlMode& mode) const {
    call_if_available(callbacks.der_control, mode);
}

void Feedback::der_curves(const message_20::datatypes::DerControl& control) const {
    call_if_available(callbacks.der_curves, control);
}

} // namespace iso15118::ev
