// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace everest::lib::API::V1_0::types::telemetry {

enum class ChargeProgress {
    Start,
    Stop,
    Renegotiate,
    Pause,
    Terminate,
};

enum class V2gCommunicationState {
    StateId0 = 0,
    StateId1 = 1,
    StateId2 = 2,
    StateId3 = 3,
    StateId4 = 4,
    StateId5 = 5,
    StateId6 = 6,
    StateId7 = 7,
    StateId8 = 8,
    StateId9 = 9,
    StateId10 = 10,
    StateId11 = 11,
    StateId12 = 12,
    StateId13 = 13,
    StateId14 = 14,
};

enum class V2gMessageState {
    SupportedAppProtocol = 0,
    SessionSetup = 1,
    ServiceDiscovery = 2,
    ServiceDetail = 3,
    PaymentServiceSelection = 4,
    PaymentDetails = 5,
    Authorization = 6,
    ChargeParameterDiscovery = 7,
    MeteringReceipt = 8,
    CertificateUpdate = 9,
    CertificateInstallation = 10,
    ChargingStatus = 11,
    CableCheck = 12,
    PreCharge = 13,
    PowerDelivery = 14,
    CurrentDemand = 15,
    WeldingDetection = 16,
    SessionStop = 17,
    Unknown = 18,
};

enum class V2gServerStatus {
    Inactive = 0,
    Active = 1,
};

enum class V2gEvErrorCode {
    NO_ERROR = 0,
    FAILED_RESSTemperatureInhibit = 1,
    FAILED_EVShiftPosition = 2,
    FAILED_ChargerConnectorLockFault = 3,
    FAILED_EVRESSMalfunction = 4,
    FAILED_ChargingCurrentdifferential = 5,
    FAILED_ChargingVoltageOutOfRange = 6,
    Reserved_A = 7,
    Reserved_B = 8,
    Reserved_C = 9,
    FAILED_ChargingSystemIncompatibility = 10,
    NoData = 11,
};

enum class SlacState {
    Init,
    Reset,
    ResetChip,
    Idle,
    Failed,
    Unmatched,
    Matching,
    WaitForLink,
    Validate,
    Matched,
};

enum class SlacD3State {
    Unmatched,
    Matching,
    Matched,
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
    V2gCommunicationState comm_state{V2gCommunicationState::StateId0};
    V2gMessageState message_state{V2gMessageState::SupportedAppProtocol};
    V2gServerStatus udp_server_status{V2gServerStatus::Inactive};
    V2gServerStatus tcp_listener_status{V2gServerStatus::Inactive};
    V2gServerStatus tcp_server_status{V2gServerStatus::Inactive};
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
    V2gEvErrorCode error_code{V2gEvErrorCode::NO_ERROR};
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

struct SlacStatus {
    bool matching_requested{false};
    bool modem_PIB{false};
    bool modem_NMK{false};
    bool modem_link_ready{false};
    int32_t session_count{0};
    float average_attenuation{0.};
    std::string ev_mac;
    SlacState match_state{SlacState::Init};
    SlacD3State d3_state{SlacD3State::Unmatched};
};

struct SlacFsmState {
    std::vector<std::string> states;
    std::map<std::string, SlacFsmState> submachines;
    std::vector<SlacFsmState> sessions;
};

} // namespace everest::lib::API::V1_0::types::telemetry
