// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/ev/config.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/session/protocol.hpp>

// Protocol-neutral, universal EV configuration. Although the charge-parameter payload structs still
// live in the d20::ev namespace (they are expressed with the -20 RationalNumber datatype), the
// EvSetupConfig / EvSessionConfig aggregates below are consumed by the ISO 15118-2 and DIN SPEC 70121
// engines as well, so they live in the protocol-neutral iso15118::session namespace.
namespace iso15118::session {

namespace dt = message_20::datatypes;

// Session-independent EV setup configuration
struct EvSetupConfig {
    std::string evcc_id;

    // Prioritized list of the energy transfer services the EV wants to use.
    // Lower index == higher priority.
    std::vector<dt::ServiceCategory> supported_energy_services;

    // Preferred control mode. The mode actually used is derived from the selected parameter set.
    dt::ControlMode preferred_control_mode{dt::ControlMode::Dynamic};

    // Supported authorization options, defaults to EIM.
    std::vector<dt::Authorization> supported_auth_options{dt::Authorization::EIM};

    std::optional<std::string> custom_protocol{std::nullopt};

    // Priority-ordered list of protocol generations offered in the SupportedAppProtocol handshake.
    // Lower index == higher priority.
    std::vector<ProtocolId> supported_protocols{ProtocolId::ISO15118_20};

    // EVCCID / MAC used by the ISO 15118-2 and DIN SPEC 70121 SessionSetup.
    std::array<uint8_t, 6> evcc_mac{0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    // Requested energy transfer mode granularity for the pre-20 protocols (AC single vs three phase,
    // or DC_extended). Selects the AC/DC branch of the ISO-2 / DIN state machines.
    message_2::datatypes::EnergyTransferMode iso2_energy_transfer_mode{
        message_2::datatypes::EnergyTransferMode::DC_extended};

    // AC charge parameters for the ISO 15118-2 AC branch.
    float iso2_ac_e_amount{60000.0f};
    float iso2_ac_ev_max_voltage{400.0f};
    float iso2_ac_ev_max_current{32.0f};
    float iso2_ac_ev_min_current{10.0f};

    // When set, re-join a paused session of the negotiated protocol (the SAP offer is constrained to
    // that protocol). Only honored by the ISO-2 and DIN engines.
    std::optional<std::array<uint8_t, 8>> resumed_session_id{std::nullopt};

    d20::ev::DcEvChargeParameters dc_charge_parameters{};
    std::optional<d20::ev::DcEvBptChargeParameters> dc_bpt_charge_parameters{std::nullopt};
    d20::ev::AcEvChargeParameters ac_charge_parameters{};
};

// Session-scoped EV configuration. Constructed from EvSetupConfig at the start of a session.
struct EvSessionConfig {
    EvSessionConfig() = default;
    explicit EvSessionConfig(EvSetupConfig config);

    std::string evcc_id;

    std::vector<dt::ServiceCategory> supported_energy_services;
    dt::ControlMode preferred_control_mode{dt::ControlMode::Dynamic};
    std::vector<dt::Authorization> supported_auth_options{dt::Authorization::EIM};
    std::optional<std::string> custom_protocol{std::nullopt};

    std::vector<ProtocolId> supported_protocols{ProtocolId::ISO15118_20};
    std::array<uint8_t, 6> evcc_mac{0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    message_2::datatypes::EnergyTransferMode iso2_energy_transfer_mode{
        message_2::datatypes::EnergyTransferMode::DC_extended};
    float iso2_ac_e_amount{60000.0f};
    float iso2_ac_ev_max_voltage{400.0f};
    float iso2_ac_ev_max_current{32.0f};
    float iso2_ac_ev_min_current{10.0f};
    std::optional<std::array<uint8_t, 8>> resumed_session_id{std::nullopt};

    d20::ev::DcEvChargeParameters dc_charge_parameters{};
    std::optional<d20::ev::DcEvBptChargeParameters> dc_bpt_charge_parameters{std::nullopt};
    d20::ev::AcEvChargeParameters ac_charge_parameters{};
};

// Protocol-neutral, universal SECC configuration. The per-service parameter list, DC/AC/DER limit and
// setup sub-structs still live in the d20 namespace (they are expressed with the -20 RationalNumber
// datatype), but the EvseSetupConfig / SessionConfig aggregates below are consumed by the ISO 15118-2
// and DIN SPEC 70121 SECC engines as well, so they live in the protocol-neutral iso15118::session
// namespace.

// Session-independent EVSE setup configuration
struct EvseSetupConfig {
    std::string evse_id;
    std::vector<message_20::datatypes::ServiceCategory> supported_energy_services;
    std::vector<message_20::datatypes::Authorization> authorization_services;
    std::vector<uint16_t> supported_vas_services;
    bool enable_certificate_install_service;
    d20::DcTransferLimits dc_limits;
    d20::AcTransferLimits ac_limits;
    std::optional<d20::IecDerTransferLimits> der_limits;
    std::vector<d20::ControlMobilityNeedsModes> control_mobility_modes;
    std::optional<std::string> custom_protocol{std::nullopt};
    std::optional<d20::AcSetupConfig> ac_setup_config{std::nullopt};
    std::optional<d20::BptSetupConfig> bpt_setup_config{std::nullopt};
    std::optional<d20::DerSetupConfig> der_setup_config{std::nullopt};
    d20::DcTransferLimits powersupply_limits;
    bool selecting_sap_based_on_energy_service{false};

    // Priority-ordered list of protocol generations the SECC accepts in the SupportedAppProtocol
    // handshake. Lower index == higher priority. Defaults to ISO 15118-20 only.
    std::vector<ProtocolId> supported_protocols{ProtocolId::ISO15118_20};

    // ISO 15118-2 Plug-and-Charge (Contract payment). When enabled the ISO-2 SECC engine offers the
    // Contract payment option and runs the PnC PaymentDetails/Authorization flow. The MO/V2G root paths
    // are used to validate the contract certificate chain.
    bool iso2_pnc_enabled{false};
    // ISO 15118-2: request a (signed) MeteringReceipt from the EV (sets ReceiptRequired in the DC
    // CurrentDemandRes / AC ChargingStatusRes charge loop). Only effective for PnC (Contract) sessions
    // per [V2G2-691]. Driven by EvseManager's ev_receipt_required config via receipt_is_required.
    bool iso2_receipt_required{false};
    std::string contract_mo_root_path{};
    std::string contract_v2g_root_path{};
};

// Session-scoped SECC configuration. Constructed from EvseSetupConfig at the start of a session. This
// should only have EVSE information.
struct SessionConfig {
    explicit SessionConfig(EvseSetupConfig);

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

    d20::DcTransferLimits dc_limits;
    d20::AcTransferLimits ac_limits;

    d20::DerSetupConfig der_setup_config;
    std::optional<d20::IecDerTransferLimits> der_limits;

    d20::DcTransferLimits powersupply_limits;

    std::vector<d20::ControlMobilityNeedsModes> supported_control_mobility_modes;

    std::optional<std::string> custom_protocol{std::nullopt};
    bool selecting_sap_based_on_energy_service{false};

    std::vector<ProtocolId> supported_protocols{ProtocolId::ISO15118_20};

    // ISO 15118-2 Plug-and-Charge (Contract payment); see EvseSetupConfig.
    bool iso2_pnc_enabled{false};
    // ISO 15118-2: request a (signed) MeteringReceipt from the EV (sets ReceiptRequired in the DC
    // CurrentDemandRes / AC ChargingStatusRes charge loop). Only effective for PnC (Contract) sessions
    // per [V2G2-691]. Driven by EvseManager's ev_receipt_required config via receipt_is_required.
    bool iso2_receipt_required{false};
    std::string contract_mo_root_path{};
    std::string contract_v2g_root_path{};
};

} // namespace iso15118::session
