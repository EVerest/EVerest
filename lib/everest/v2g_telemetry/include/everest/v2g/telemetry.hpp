// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace everest::lib::v2g {

enum class ChargeProgress {
    Start,
    Stop,
    Renegotiate,
    Pause,
    Terminate,
};

struct V2gTransport {
    int comm_state{0};
    int message_state{0};
    int udp_server_status{0};
    int tcp_listener_status{0};
    int tcp_server_status{0};
    bool tcp_connection_established{false};
    bool tcp_discovery_enable{false};
    bool tcp_security_enable{false};
    bool tcp_security_required{false};
    int tcp_port_number{0};
    bool session_setup_requested{false};
    bool authorization_requested{false};
    bool charge_parameter_discovery_requested{false};
    bool cable_check_requested{false};
    bool pre_charge_requested{false};
    bool current_demand_requested{false};
    bool welding_detection_requested{false};
};

struct V2gEvElectrical {
    ChargeProgress charge_progress{ChargeProgress::Start};
    int battery_soc_percent{0};
    int error_code{0};
    double maximum_current_A{0.};
    double maximum_power_W{0.};
    double maximum_voltage_V{0.};
    double maximum_rated_current_A{0.};
    double maximum_rated_power_W{0.};
    double maximum_rated_voltage_V{0.};
    double target_current_A{0.};
    double target_voltage_V{0.};
    int remaining_time_bulk_min{0};
    int remaining_time_full_min{0};
    double energy_request_Wh{0.};
    double energy_capacity_Wh{0.};
};

struct V2gPaymentService {
    bool certificate_install_requested{false};
    bool certificate_update_requested{false};
    bool charging_service_requested{false};
    bool external_payment_requested{false};
    bool contract_payment_requested{false};
    bool contract_payment_approved{false};
    bool contract_payment_error{false};
    bool internet_ftp20_requested{false};
    bool internet_ftp21_requested{false};
    bool internet_http80_requested{false};
    bool internet_https443_requested{false};
};

struct V2gChargerStatus {
    std::string evcc_id;            // "XX:XX:XX:XX:XX:XX"
    std::string selected_protocol;  // "DIN70121" / "ISO15118-2-2010" / "ISO15118-2-2013"
    bool cable_check_status{false};
    bool param_discovery_finished{false};
    std::string isolation_status;
    double dynamic_max_current_A{0.};
    double dynamic_max_power_W{0.};
    double dynamic_max_voltage_V{0.};
};

struct V2gEvseElectrical {
    double present_current_A{0.};
    double present_voltage_V{0.};
    double maximum_rated_current_A{0.};
    double maximum_rated_power_W{0.};
    double maximum_rated_voltage_V{0.};
    double minimum_rated_current_A{0.};
    double minimum_rated_voltage_V{0.};
    double current_ripple_A{0.};
    double current_tolerance_A{0.};
};

void to_json(nlohmann::json& j, ChargeProgress const&) noexcept;
void from_json(nlohmann::json const& j, ChargeProgress&);

void to_json(nlohmann::json& j, V2gTransport const&) noexcept;
void from_json(nlohmann::json const& j, V2gTransport&);

void to_json(nlohmann::json& j, V2gEvElectrical const&) noexcept;
void from_json(nlohmann::json const& j, V2gEvElectrical&);

void to_json(nlohmann::json& j, V2gPaymentService const&) noexcept;
void from_json(nlohmann::json const& j, V2gPaymentService&);

void to_json(nlohmann::json& j, V2gChargerStatus const&) noexcept;
void from_json(nlohmann::json const& j, V2gChargerStatus&);

void to_json(nlohmann::json& j, V2gEvseElectrical const&) noexcept;
void from_json(nlohmann::json const& j, V2gEvseElectrical&);

std::string serialize(V2gTransport const&);
std::string serialize(V2gEvElectrical const&);
std::string serialize(V2gPaymentService const&);
std::string serialize(V2gChargerStatus const&);
std::string serialize(V2gEvseElectrical const&);

V2gTransport deserialize_transport(std::string const&);
V2gEvElectrical deserialize_ev_electrical(std::string const&);
V2gPaymentService deserialize_payment_service(std::string const&);
V2gChargerStatus deserialize_charger_status(std::string const&);
V2gEvseElectrical deserialize_evse_electrical(std::string const&);

} // namespace everest::lib::v2g
