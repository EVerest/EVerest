// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "PeerMocks.hpp"

#include <sstream>
#include <string>

namespace module::test {

namespace {

std::string to_string(bool v) {
    return v ? "true" : "false";
}

std::string to_string(double v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

std::string to_string(::types::ev_board_support::EvCpState s) {
    return ::types::ev_board_support::ev_cp_state_to_string(s);
}

std::string to_string(::types::iso15118::EnergyTransferMode m) {
    return ::types::iso15118::energy_transfer_mode_to_string(m);
}

std::string to_string(const ::types::iso15118::SelectedPaymentOption& o) {
    if (o.payment_option) {
        return ::types::iso15118::payment_option_to_string(*o.payment_option);
    }
    return "unset";
}

} // namespace

// ---- MockBoardSupport ---------------------------------------------------

void MockBoardSupport::call_enable(bool value) {
    records.emplace_back("enable(value=" + to_string(value) + ")");
}

void MockBoardSupport::call_set_cp_state(::types::ev_board_support::EvCpState cp_state) {
    records.emplace_back("set_cp_state(cp_state=" + to_string(cp_state) + ")");
}

void MockBoardSupport::call_allow_power_on(bool value) {
    records.emplace_back("allow_power_on(value=" + to_string(value) + ")");
}

void MockBoardSupport::call_diode_fail(bool value) {
    records.emplace_back("diode_fail(value=" + to_string(value) + ")");
}

void MockBoardSupport::call_set_ac_max_current(double current) {
    records.emplace_back("set_ac_max_current(current=" + to_string(current) + ")");
}

void MockBoardSupport::call_set_three_phases(bool three_phases) {
    records.emplace_back("set_three_phases(three_phases=" + to_string(three_phases) + ")");
}

void MockBoardSupport::call_set_rcd_error(double rcd_current_mA) {
    records.emplace_back("set_rcd_error(rcd_current_mA=" + to_string(rcd_current_mA) + ")");
}

// ---- MockIso15118Ev -----------------------------------------------------

bool MockIso15118Ev::call_start_charging(::types::iso15118::EnergyTransferMode mode,
                                         ::types::iso15118::SelectedPaymentOption payment, double departure_time,
                                         double e_amount) {
    std::ostringstream os;
    os << "start_charging(mode=" << to_string(mode) << ",payment=" << to_string(payment)
       << ",departure_time=" << to_string(departure_time) << ",e_amount=" << to_string(e_amount) << ")";
    records.emplace_back(os.str());
    return next_start_charging_result;
}

void MockIso15118Ev::call_stop_charging() {
    records.emplace_back("stop_charging()");
}

void MockIso15118Ev::call_pause_charging() {
    records.emplace_back("pause_charging()");
}

void MockIso15118Ev::call_set_fault() {
    records.emplace_back("set_fault()");
}

void MockIso15118Ev::call_set_dc_params(const ::types::iso15118::DcEvParameters& params) {
    std::ostringstream os;
    os << "set_dc_params(target_current=" << to_string(params.target_current.value_or(-1))
       << ",target_voltage=" << to_string(params.target_voltage.value_or(-1))
       << ",max_current_limit=" << to_string(params.max_current_limit.value_or(-1))
       << ",max_power_limit=" << to_string(params.max_power_limit.value_or(-1))
       << ",max_voltage_limit=" << to_string(params.max_voltage_limit.value_or(-1))
       << ",energy_capacity=" << to_string(params.energy_capacity.value_or(-1)) << ")";
    records.emplace_back(os.str());
}

void MockIso15118Ev::call_set_bpt_dc_params(const ::types::iso15118::DcEvBPTParameters&) {
    records.emplace_back("set_bpt_dc_params(...)");
}

void MockIso15118Ev::call_enable_sae_j2847_v2g_v2h() {
    records.emplace_back("enable_sae_j2847_v2g_v2h()");
}

void MockIso15118Ev::call_update_soc(double soc) {
    records.emplace_back("update_soc(SoC=" + to_string(soc) + ")");
}

// ---- MockEvSlac ---------------------------------------------------------

void MockEvSlac::call_reset() {
    records.emplace_back("reset()");
}

bool MockEvSlac::call_trigger_matching() {
    records.emplace_back("trigger_matching()");
    return next_trigger_matching_result;
}

// ---- MockKvs ------------------------------------------------------------

void MockKvs::call_store(const std::string& key, Value /*value*/) {
    records.emplace_back("store(key=" + key + ")");
}

MockKvs::Value MockKvs::call_load(const std::string& key) {
    records.emplace_back("load(key=" + key + ")");
    return next_load_value;
}

void MockKvs::call_delete(const std::string& key) {
    records.emplace_back("delete(key=" + key + ")");
}

bool MockKvs::call_exists(const std::string& key) {
    records.emplace_back("exists(key=" + key + ")");
    return next_exists_result;
}

} // namespace module::test
