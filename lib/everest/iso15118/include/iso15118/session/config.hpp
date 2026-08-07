// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/session/protocol.hpp>

namespace iso15118::session {

namespace dt = message_20::datatypes;

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
    // Accept a contract whose chain cannot be validated locally (missing MO root) and forward it to
    // the CSMS for central validation (OCPP CentralContractValidationAllowed, EvseV2G parity).
    bool central_contract_validation_allowed{false};
    // ISO 15118-2 AC: latest EVSE maximum current (per phase, A) from EvseManager's
    // update_ac_max_current cmd. When set it overrides the power-derived default for the -2 session's
    // EVSEMaxCurrent; mid-session changes additionally reach the running charge loop as an
    // UpdateAcMaxCurrent control event.
    std::optional<float> iso2_ac_max_current{std::nullopt};
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
    // See EvseSetupConfig::central_contract_validation_allowed.
    bool central_contract_validation_allowed{false};
    // See EvseSetupConfig::iso2_ac_max_current.
    std::optional<float> iso2_ac_max_current{std::nullopt};
};

} // namespace iso15118::session
