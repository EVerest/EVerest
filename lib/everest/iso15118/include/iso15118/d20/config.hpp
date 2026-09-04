// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <ctime>
#include <map>
#include <optional>
#include <vector>

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/message/common_types.hpp>

namespace iso15118::d20 {

struct ControlMobilityNeedsModes {
    message_20::datatypes::ControlMode control_mode;
    message_20::datatypes::MobilityNeedsMode mobility_mode;
};

struct AcSetupConfig {
    uint32_t voltage;
    std::vector<message_20::datatypes::AcConnector> connectors;
};

struct BptSetupConfig {
    message_20::datatypes::BptChannel bpt_channel;
    message_20::datatypes::GeneratorMode generator_mode;
    std::optional<message_20::datatypes::GridCodeIslandingDetectionMethod> grid_code_detection_method;
};

struct DerIecSetupConfig {
    std::map<iec::DERControlName, iec::DERControlFunction> supported_der_control_functions;
    iec::OperatingMode operating_mode;
    iec::GridConnectionMode grid_connection_mode;
};

/// \brief Complete but deliberately inert default SAE grid code configuration.
///
/// Every enable and permit_service is false, so it does nothing. Values stay schema conformant, and every
/// mandatory curve carries the two data points the schema requires as a minimum. This is not a real grid
/// code: a deployment needing actual grid-code behavior must supply its own configuration.
///
/// The two EnterService voltage bands and the VoltVar reference voltage are volts, not percentages
/// (AMD1 Table 1), so they are derived from nominal_voltage_v.
sae::DERControl get_default_sae_der_control(float nominal_voltage_v);

struct DerSaeSetupConfig {
    explicit DerSaeSetupConfig(sae::DERControl der_control_, sae::RequiredDEROperatingMode op_mode,
                               sae::GridConnectionMode conn_mode) :
        der_control(std::move(der_control_)),
        required_der_operating_mode(op_mode),
        grid_connection_mode(conn_mode),
        der_control_update_time(static_cast<std::uint64_t>(std::time(nullptr))) {
    }

    sae::DERControl der_control{};
    sae::RequiredDEROperatingMode required_der_operating_mode{sae::RequiredDEROperatingMode::GridFollowing};
    sae::GridConnectionMode grid_connection_mode{sae::GridConnectionMode::GridConnected};
    std::uint64_t der_control_update_time{0}; // SECC time
};

/// Inert default grid code with GridFollowing/GridConnected; not a real grid code.
DerSaeSetupConfig make_inert_default_sae_setup_config(float nominal_voltage_v);

struct EvseSetupConfig {
    std::string evse_id;
    std::vector<message_20::datatypes::ServiceCategory> supported_energy_services;
    std::vector<message_20::datatypes::Authorization> authorization_services;
    std::vector<uint16_t> supported_vas_services;
    bool enable_certificate_install_service;
    d20::DcTransferLimits dc_limits;
    d20::AcTransferLimits ac_limits;
    std::optional<d20::IecDerTransferLimits> der_iec_limits;
    std::optional<d20::SaeDerTransferLimits> der_sae_limits;
    std::vector<ControlMobilityNeedsModes> control_mobility_modes;
    std::optional<std::string> custom_protocol{std::nullopt};
    std::optional<AcSetupConfig> ac_setup_config{std::nullopt};
    std::optional<BptSetupConfig> bpt_setup_config{std::nullopt};
    std::optional<DerIecSetupConfig> der_iec_setup_config{std::nullopt};
    std::optional<DerSaeSetupConfig> der_sae_setup_config{std::nullopt};
    d20::DcTransferLimits powersupply_limits;
    bool selecting_sap_based_on_energy_service{false};
};

// This should only have EVSE information
struct SessionConfig {
    explicit SessionConfig(EvseSetupConfig);

    /// \brief Replaces the offered energy services.
    ///
    /// Every replacement runs the same AC_DER_SAE offer rules as the constructor, so a non-conformant
    /// AC_DER_SAE cannot re-enter the offer through a mid session service update.
    void set_supported_energy_transfer_services(std::vector<message_20::datatypes::ServiceCategory> services);

    std::string evse_id;

    bool cert_install_service;
    std::vector<message_20::datatypes::Authorization> authorization_services;

    std::vector<message_20::datatypes::ServiceCategory> supported_energy_transfer_services;
    std::vector<std::uint16_t> supported_vas_services;

    std::vector<message_20::datatypes::AcParameterList> ac_parameter_list;
    std::vector<message_20::datatypes::AcBptParameterList> ac_bpt_parameter_list;
    std::vector<message_20::datatypes::AcDerParameterList> ac_der_iec_parameter_list;
    std::vector<message_20::datatypes::DcParameterList> dc_parameter_list;
    std::vector<message_20::datatypes::DcBptParameterList> dc_bpt_parameter_list;

    std::vector<message_20::datatypes::McsParameterList> mcs_parameter_list;
    std::vector<message_20::datatypes::McsBptParameterList> mcs_bpt_parameter_list;

    std::vector<message_20::datatypes::InternetParameterList> internet_parameter_list;
    std::vector<message_20::datatypes::ParkingParameterList> parking_parameter_list;

    DcTransferLimits dc_limits;
    AcTransferLimits ac_limits;

    DerIecSetupConfig der_iec_setup_config;
    std::optional<IecDerTransferLimits> der_iec_limits;

    std::optional<DerSaeSetupConfig> der_sae_setup_config;
    std::optional<SaeDerTransferLimits> der_sae_limits;

    DcTransferLimits powersupply_limits;

    std::vector<ControlMobilityNeedsModes> supported_control_mobility_modes;

    std::optional<std::string> custom_protocol{std::nullopt};
    bool selecting_sap_based_on_energy_service{false};
};

} // namespace iso15118::d20
