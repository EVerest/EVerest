// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/shared_datatypes.hpp>
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
    // Physical EVSE parameters from EvseManager's set_charging_parameters cmd. Used by the ISO 15118-2
    // and DIN SPEC 70121 engines; see d20::PhysicalValues.
    d20::PhysicalValues physical_values{};
    // Pending no-energy pause request (IEC 61851-23:2023 CC.3.5.3). Armed by the module's
    // no_energy_pause_charging cmd and consumed by the next session that starts, so it never leaks into
    // a later session; a request arriving while a session is running reaches it as a NoEnergyPause
    // control event instead.
    d20::NoEnergyPauseMode no_energy_pause{d20::NoEnergyPauseMode::None};
    // The pre-20 energy transfer modes the module configured (update_energy_transfer_modes), verbatim.
    // supported_energy_services above is the -20 view (modes collapsed into service categories, which
    // loses e.g. the DC_core/DC_extended distinction); the ISO 15118-2 and DIN SPEC 70121 engines
    // advertise these instead when the module provided them. Applied at session start.
    std::vector<shared_datatypes::EnergyTransferMode> pre20_energy_transfer_modes{};

    // How long the SECC keeps answering EVSEProcessing=Ongoing while waiting for the authorization
    // result before it fails the session, in SECONDS; 0 means wait indefinitely. Defaults are EvseV2G's
    // (auth_timeout_eim / auth_timeout_pnc): EIM gets far more than the 55 s
    // V2G_SECC_Ongoing_Performance_Time of [V2G2-712/713] on purpose, because the bottleneck is a human
    // presenting an RFID card or confirming in an app, not SECC processing.
    //
    // Consumed by the ISO 15118-2 engine (EIM and PnC, selected by the payment option the EV chose) and,
    // for EIM, by the DIN SPEC 70121 engine, which knows no other payment option. ISO 15118-20 is
    // deliberately NOT covered: it keeps its own fixed d20::TIMEOUT_EIM_ONGOING.
    uint32_t auth_timeout_eim_s{300};
    uint32_t auth_timeout_pnc_s{55};
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
    // See EvseSetupConfig::physical_values.
    d20::PhysicalValues physical_values{};
    // See EvseSetupConfig::no_energy_pause.
    d20::NoEnergyPauseMode no_energy_pause{d20::NoEnergyPauseMode::None};
    // See EvseSetupConfig::pre20_energy_transfer_modes.
    std::vector<shared_datatypes::EnergyTransferMode> pre20_energy_transfer_modes{};
    // See EvseSetupConfig::auth_timeout_eim_s / auth_timeout_pnc_s (seconds, 0 = indefinitely).
    uint32_t auth_timeout_eim_s{300};
    uint32_t auth_timeout_pnc_s{55};
};

// Converts one of the authorization timeouts above from seconds to the milliseconds the engines' timeout
// slots take, saturating instead of wrapping (a configured value beyond ~49 days would overflow the
// uint32_t millisecond counter). 0 passes through unchanged and means "no timeout".
uint32_t auth_timeout_to_ms(uint32_t timeout_s);

} // namespace iso15118::session
