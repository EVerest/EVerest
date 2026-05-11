// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "telemetry/v2g.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace everest::lib::API::V1_0::types::telemetry {

using json = nlohmann::json;

namespace {

template <typename T>
void load(json const& j, char const* key, T& dst) {
    if (j.contains(key)) {
        dst = j.at(key).get<T>();
    }
}

} // namespace

void to_json(json& j, ChargeProgress const& k) noexcept {
    switch (k) {
    case ChargeProgress::Start:       j = "Start"; return;
    case ChargeProgress::Stop:        j = "Stop"; return;
    case ChargeProgress::Renegotiate: j = "Renegotiate"; return;
    case ChargeProgress::Pause:       j = "Pause"; return;
    case ChargeProgress::Terminate:   j = "Terminate"; return;
    }
    j = "INVALID_everest::lib::API::V1_0::types::telemetry::ChargeProgress";
}

void from_json(json const& j, ChargeProgress& k) {
    std::string s = j.get<std::string>();
    if (s == "Start")       { k = ChargeProgress::Start; return; }
    if (s == "Stop")        { k = ChargeProgress::Stop; return; }
    if (s == "Renegotiate") { k = ChargeProgress::Renegotiate; return; }
    if (s == "Pause")       { k = ChargeProgress::Pause; return; }
    if (s == "Terminate")   { k = ChargeProgress::Terminate; return; }
    throw std::out_of_range("Cannot convert \"" + s + "\" to everest::lib::API::V1_0::types::telemetry::ChargeProgress");
}

void to_json(json& j, V2gTransport const& k) noexcept {
    j["comm_state"] = k.comm_state;
    j["message_state"] = k.message_state;
    j["udp_server_status"] = k.udp_server_status;
    j["tcp_listener_status"] = k.tcp_listener_status;
    j["tcp_server_status"] = k.tcp_server_status;
    j["tcp_connection_established"] = k.tcp_connection_established;
    j["tcp_discovery_enable"] = k.tcp_discovery_enable;
    j["tcp_security_enable"] = k.tcp_security_enable;
    j["tcp_security_required"] = k.tcp_security_required;
    j["tcp_port_number"] = k.tcp_port_number;
    j["session_setup_requested"] = k.session_setup_requested;
    j["authorization_requested"] = k.authorization_requested;
    j["charge_parameter_discovery_requested"] = k.charge_parameter_discovery_requested;
    j["cable_check_requested"] = k.cable_check_requested;
    j["pre_charge_requested"] = k.pre_charge_requested;
    j["current_demand_requested"] = k.current_demand_requested;
    j["welding_detection_requested"] = k.welding_detection_requested;
}

void from_json(json const& j, V2gTransport& k) {
    load(j, "comm_state", k.comm_state);
    load(j, "message_state", k.message_state);
    load(j, "udp_server_status", k.udp_server_status);
    load(j, "tcp_listener_status", k.tcp_listener_status);
    load(j, "tcp_server_status", k.tcp_server_status);
    load(j, "tcp_connection_established", k.tcp_connection_established);
    load(j, "tcp_discovery_enable", k.tcp_discovery_enable);
    load(j, "tcp_security_enable", k.tcp_security_enable);
    load(j, "tcp_security_required", k.tcp_security_required);
    load(j, "tcp_port_number", k.tcp_port_number);
    load(j, "session_setup_requested", k.session_setup_requested);
    load(j, "authorization_requested", k.authorization_requested);
    load(j, "charge_parameter_discovery_requested", k.charge_parameter_discovery_requested);
    load(j, "cable_check_requested", k.cable_check_requested);
    load(j, "pre_charge_requested", k.pre_charge_requested);
    load(j, "current_demand_requested", k.current_demand_requested);
    load(j, "welding_detection_requested", k.welding_detection_requested);
}

void to_json(json& j, V2gEvElectrical const& k) noexcept {
    j["charge_progress"] = k.charge_progress;
    j["battery_soc_percent"] = k.battery_soc_percent;
    j["error_code"] = k.error_code;
    j["maximum_current_A"] = k.maximum_current_A;
    j["maximum_power_W"] = k.maximum_power_W;
    j["maximum_voltage_V"] = k.maximum_voltage_V;
    j["maximum_rated_current_A"] = k.maximum_rated_current_A;
    j["maximum_rated_power_W"] = k.maximum_rated_power_W;
    j["maximum_rated_voltage_V"] = k.maximum_rated_voltage_V;
    j["target_current_A"] = k.target_current_A;
    j["target_voltage_V"] = k.target_voltage_V;
    j["remaining_time_bulk_min"] = k.remaining_time_bulk_min;
    j["remaining_time_full_min"] = k.remaining_time_full_min;
    j["energy_request_Wh"] = k.energy_request_Wh;
    j["energy_capacity_Wh"] = k.energy_capacity_Wh;
}

void from_json(json const& j, V2gEvElectrical& k) {
    if (j.contains("charge_progress")) {
        k.charge_progress = j.at("charge_progress").get<ChargeProgress>();
    }
    load(j, "battery_soc_percent", k.battery_soc_percent);
    load(j, "error_code", k.error_code);
    load(j, "maximum_current_A", k.maximum_current_A);
    load(j, "maximum_power_W", k.maximum_power_W);
    load(j, "maximum_voltage_V", k.maximum_voltage_V);
    load(j, "maximum_rated_current_A", k.maximum_rated_current_A);
    load(j, "maximum_rated_power_W", k.maximum_rated_power_W);
    load(j, "maximum_rated_voltage_V", k.maximum_rated_voltage_V);
    load(j, "target_current_A", k.target_current_A);
    load(j, "target_voltage_V", k.target_voltage_V);
    load(j, "remaining_time_bulk_min", k.remaining_time_bulk_min);
    load(j, "remaining_time_full_min", k.remaining_time_full_min);
    load(j, "energy_request_Wh", k.energy_request_Wh);
    load(j, "energy_capacity_Wh", k.energy_capacity_Wh);
}

void to_json(json& j, V2gPaymentService const& k) noexcept {
    j["certificate_install_requested"] = k.certificate_install_requested;
    j["certificate_update_requested"] = k.certificate_update_requested;
    j["charging_service_requested"] = k.charging_service_requested;
    j["external_payment_requested"] = k.external_payment_requested;
    j["contract_payment_requested"] = k.contract_payment_requested;
    j["contract_payment_approved"] = k.contract_payment_approved;
    j["contract_payment_error"] = k.contract_payment_error;
    j["internet_ftp20_requested"] = k.internet_ftp20_requested;
    j["internet_ftp21_requested"] = k.internet_ftp21_requested;
    j["internet_http80_requested"] = k.internet_http80_requested;
    j["internet_https443_requested"] = k.internet_https443_requested;
}

void from_json(json const& j, V2gPaymentService& k) {
    load(j, "certificate_install_requested", k.certificate_install_requested);
    load(j, "certificate_update_requested", k.certificate_update_requested);
    load(j, "charging_service_requested", k.charging_service_requested);
    load(j, "external_payment_requested", k.external_payment_requested);
    load(j, "contract_payment_requested", k.contract_payment_requested);
    load(j, "contract_payment_approved", k.contract_payment_approved);
    load(j, "contract_payment_error", k.contract_payment_error);
    load(j, "internet_ftp20_requested", k.internet_ftp20_requested);
    load(j, "internet_ftp21_requested", k.internet_ftp21_requested);
    load(j, "internet_http80_requested", k.internet_http80_requested);
    load(j, "internet_https443_requested", k.internet_https443_requested);
}

void to_json(json& j, V2gChargerStatus const& k) noexcept {
    j["evcc_id"] = k.evcc_id;
    j["selected_protocol"] = k.selected_protocol;
    j["cable_check_status"] = k.cable_check_status;
    j["param_discovery_finished"] = k.param_discovery_finished;
    j["isolation_status"] = k.isolation_status;
    j["dynamic_max_current_A"] = k.dynamic_max_current_A;
    j["dynamic_max_power_W"] = k.dynamic_max_power_W;
    j["dynamic_max_voltage_V"] = k.dynamic_max_voltage_V;
}

void from_json(json const& j, V2gChargerStatus& k) {
    load(j, "evcc_id", k.evcc_id);
    load(j, "selected_protocol", k.selected_protocol);
    load(j, "cable_check_status", k.cable_check_status);
    load(j, "param_discovery_finished", k.param_discovery_finished);
    load(j, "isolation_status", k.isolation_status);
    load(j, "dynamic_max_current_A", k.dynamic_max_current_A);
    load(j, "dynamic_max_power_W", k.dynamic_max_power_W);
    load(j, "dynamic_max_voltage_V", k.dynamic_max_voltage_V);
}

void to_json(json& j, V2gEvseElectrical const& k) noexcept {
    j["present_current_A"] = k.present_current_A;
    j["present_voltage_V"] = k.present_voltage_V;
    j["maximum_rated_current_A"] = k.maximum_rated_current_A;
    j["maximum_rated_power_W"] = k.maximum_rated_power_W;
    j["maximum_rated_voltage_V"] = k.maximum_rated_voltage_V;
    j["minimum_rated_current_A"] = k.minimum_rated_current_A;
    j["minimum_rated_voltage_V"] = k.minimum_rated_voltage_V;
    j["current_ripple_A"] = k.current_ripple_A;
    j["current_tolerance_A"] = k.current_tolerance_A;
}

void from_json(json const& j, V2gEvseElectrical& k) {
    load(j, "present_current_A", k.present_current_A);
    load(j, "present_voltage_V", k.present_voltage_V);
    load(j, "maximum_rated_current_A", k.maximum_rated_current_A);
    load(j, "maximum_rated_power_W", k.maximum_rated_power_W);
    load(j, "maximum_rated_voltage_V", k.maximum_rated_voltage_V);
    load(j, "minimum_rated_current_A", k.minimum_rated_current_A);
    load(j, "minimum_rated_voltage_V", k.minimum_rated_voltage_V);
    load(j, "current_ripple_A", k.current_ripple_A);
    load(j, "current_tolerance_A", k.current_tolerance_A);
}

std::string serialize(V2gTransport const& v)       { return json(v).dump(0); }
std::string serialize(V2gEvElectrical const& v)    { return json(v).dump(0); }
std::string serialize(V2gPaymentService const& v)  { return json(v).dump(0); }
std::string serialize(V2gChargerStatus const& v)   { return json(v).dump(0); }
std::string serialize(V2gEvseElectrical const& v)  { return json(v).dump(0); }

V2gTransport       deserialize_transport(std::string const& s)        { return json::parse(s).get<V2gTransport>(); }
V2gEvElectrical    deserialize_ev_electrical(std::string const& s)    { return json::parse(s).get<V2gEvElectrical>(); }
V2gPaymentService  deserialize_payment_service(std::string const& s)  { return json::parse(s).get<V2gPaymentService>(); }
V2gChargerStatus   deserialize_charger_status(std::string const& s)   { return json::parse(s).get<V2gChargerStatus>(); }
V2gEvseElectrical  deserialize_evse_electrical(std::string const& s)  { return json::parse(s).get<V2gEvseElectrical>(); }

} // namespace everest::lib::API::V1_0::types::telemetry
