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

// The sub-structs still live in the d20 namespace (they use the -20 RationalNumber datatype), but
// these aggregates are consumed by the ISO 15118-2 and DIN SPEC 70121 engines too.

struct VasService {
    uint16_t id{0};
    std::optional<std::string> name{std::nullopt};  // ServiceName, max 32 characters
    std::optional<std::string> scope{std::nullopt}; // ServiceScope, max 64 characters
    bool free_service{true};
};

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
    std::vector<d20::ControlMobilityNeedsModes> control_mobility_modes;
    std::optional<std::string> custom_protocol{std::nullopt};
    std::optional<d20::AcSetupConfig> ac_setup_config{std::nullopt};
    std::optional<d20::BptSetupConfig> bpt_setup_config{std::nullopt};
    std::optional<d20::DerIecSetupConfig> der_iec_setup_config{std::nullopt};
    std::optional<d20::DerSaeSetupConfig> der_sae_setup_config{std::nullopt};
    d20::DcTransferLimits powersupply_limits;
    bool selecting_sap_based_on_energy_service{false};

    // Priority-ordered: lower index == higher priority. Defaults to ISO 15118-20 only.
    std::vector<ProtocolId> supported_protocols{ProtocolId::ISO15118_20};

    // The MO/V2G root paths validate the contract certificate chain.
    bool iso2_pnc_enabled{false};
    // Only effective for PnC (Contract) sessions per [V2G2-691].
    bool iso2_receipt_required{false};
    std::string contract_mo_root_path{};
    std::string contract_v2g_root_path{};
    // Forwarded to the CSMS instead (OCPP CentralContractValidationAllowed, EvseV2G parity).
    bool central_contract_validation_allowed{false};
    // Per phase, in A. Overrides the power-derived default; mid-session changes additionally reach the
    // running charge loop as an UpdateAcMaxCurrent control event.
    std::optional<float> iso2_ac_max_current{std::nullopt};
    d20::PhysicalValues physical_values{};
    // Consumed by the next session that starts, so it never leaks into a later one; a request arriving
    // while a session runs reaches it as a NoEnergyPause control event instead.
    d20::NoEnergyPauseMode no_energy_pause{d20::NoEnergyPauseMode::None};
    // Verbatim, because supported_energy_services above is the -20 view, which collapses modes into
    // service categories and loses e.g. the DC_core/DC_extended distinction.
    std::vector<shared_datatypes::EnergyTransferMode> pre20_energy_transfer_modes{};
    // supported_vas_services above is the -20 view (ids only); ISO 15118-2 also needs name, scope and
    // the free-of-charge flag per service.
    std::vector<VasService> pre20_vas_services{};

    // In SECONDS; 0 waits indefinitely. EIM gets far more than the 55 s V2G_SECC_Ongoing_Performance_Time
    // of [V2G2-712/713] on purpose: the bottleneck is a human presenting a card, not SECC processing.
    // ISO 15118-20 is deliberately not covered -- it keeps its own fixed d20::TIMEOUT_EIM_ONGOING.
    uint32_t auth_timeout_eim_s{300};
    uint32_t auth_timeout_pnc_s{55};
};

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

    d20::DcTransferLimits dc_limits;
    d20::AcTransferLimits ac_limits;

    d20::DerIecSetupConfig der_iec_setup_config;
    std::optional<d20::IecDerTransferLimits> der_iec_limits;

    std::optional<d20::DerSaeSetupConfig> der_sae_setup_config;
    std::optional<d20::SaeDerTransferLimits> der_sae_limits;

    d20::DcTransferLimits powersupply_limits;

    std::vector<d20::ControlMobilityNeedsModes> supported_control_mobility_modes;

    std::optional<std::string> custom_protocol{std::nullopt};
    bool selecting_sap_based_on_energy_service{false};

    std::vector<ProtocolId> supported_protocols{ProtocolId::ISO15118_20};

    bool iso2_pnc_enabled{false};
    // Only effective for PnC (Contract) sessions per [V2G2-691].
    bool iso2_receipt_required{false};
    std::string contract_mo_root_path{};
    std::string contract_v2g_root_path{};
    bool central_contract_validation_allowed{false};
    std::optional<float> iso2_ac_max_current{std::nullopt};
    d20::PhysicalValues physical_values{};
    d20::NoEnergyPauseMode no_energy_pause{d20::NoEnergyPauseMode::None};
    std::vector<shared_datatypes::EnergyTransferMode> pre20_energy_transfer_modes{};
    std::vector<VasService> pre20_vas_services{};
    uint32_t auth_timeout_eim_s{300};
    uint32_t auth_timeout_pnc_s{55};
};

// Saturates instead of wrapping: a configured value beyond ~49 days would overflow the uint32_t
// millisecond counter. 0 passes through unchanged and means "no timeout".
uint32_t auth_timeout_to_ms(uint32_t timeout_s);

} // namespace iso15118::session
