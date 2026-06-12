// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <string>

namespace everest::lib::API::V1_0::types::telemetry {

enum class ChargeProgress {
    Start,
    Stop,
    Renegotiate,
    Pause,
    Terminate,
};

struct CertChainState {
    bool configured{false};
    bool synced{false};
    int32_t num_files{0};
    int32_t num_useful_files{0};
};

struct CertTelemetry {
    bool config_complete{false};
    bool sync_complete{false};
    CertChainState secc_chain;
    CertChainState mo_root;
};

struct EvseControlStatus {
    bool authorisation_finished{false};
    bool emergency_stop{false};
    bool error_stop{false};
    bool normal_stop{false};
    bool contract_payment_enabled{false};
    bool free_charging_enabled{false};
};

struct V2gTransport {
    int32_t comm_state{0};
    int32_t message_state{0};
    int32_t udp_server_status{0};
    int32_t tcp_listener_status{0};
    int32_t tcp_server_status{0};
    bool tcp_connection_established{false};
    bool tcp_discovery_enable{false};
    bool tcp_security_enable{false};
    bool tcp_security_required{false};
    int32_t tcp_port_number{0};
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
    int32_t battery_soc_percent{0};
    int32_t error_code{0};
    float maximum_current_A{0.};
    float maximum_power_W{0.};
    float maximum_voltage_V{0.};
    float maximum_rated_current_A{0.};
    float maximum_rated_power_W{0.};
    float maximum_rated_voltage_V{0.};
    float target_current_A{0.};
    float target_voltage_V{0.};
    int32_t remaining_time_bulk_min{0};
    int32_t remaining_time_full_min{0};
    float energy_request_Wh{0.};
    float energy_capacity_Wh{0.};
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
    std::string evcc_id;           // "XX:XX:XX:XX:XX:XX"
    std::string selected_protocol; // "DIN70121" / "ISO15118-2-2010" / "ISO15118-2-2013"
    bool cable_check_status{false};
    bool param_discovery_finished{false};
    std::string isolation_status;
    float dynamic_max_current_A{0.};
    float dynamic_max_power_W{0.};
    float dynamic_max_voltage_V{0.};
};

struct V2gEvseElectrical {
    float present_current_A{0.};
    float present_voltage_V{0.};
    float maximum_rated_current_A{0.};
    float maximum_rated_power_W{0.};
    float maximum_rated_voltage_V{0.};
    float minimum_rated_current_A{0.};
    float minimum_rated_voltage_V{0.};
    float current_ripple_A{0.};
    float current_tolerance_A{0.};
};

} // namespace everest::lib::API::V1_0::types::telemetry
