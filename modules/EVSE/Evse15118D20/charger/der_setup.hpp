// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <bitset>
#include <cstdint>
#include <optional>
#include <vector>

#include <generated/types/grid_support.hpp>
#include <generated/types/iso15118.hpp>
#include <iso15118/d20/config.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/message/common_types.hpp>

namespace module {

/// \brief Build the IEC DER transfer limits from the module's AC transfer limits.
///
/// Charge and discharge pass through from ac_limits (the discharge sign is already applied upstream).
/// Nominal and max discharge are set equal, satisfying [V2G20-3229], which compares the two by absolute
/// value. Its charge-side counterpart [V2G20-3228] is a plain <=, not a magnitude compare. Both are Annex M
/// (SAE) only; Annex L states no nominal-vs-maximum requirement, so this is defensive here. When no
/// discharge limit is present the DER discharge limits are zero. DER control directives reach the EV
/// through the control-function relay, not here.
iso15118::d20::IecDerTransferLimits build_iec_der_transfer_limits(const iso15118::d20::AcTransferLimits& ac_limits);

/// \brief Build the SAE DER transfer limits from the module's AC transfer limits and grid parameters.
///
/// Nominal charge is taken from the ac_limits charge maximum, and both nominal and max discharge from the
/// ac_limits discharge maximum, so the discharge pair satisfies [V2G20-3229] (magnitude compare) and the
/// charge pair [V2G20-3228] (plain <=). When no discharge limit is present the DER discharge limits are
/// zero. The discharge sign follows ac_limits, so it is negative only when the module's
/// negative_bidirectional_limits option is set. Note that the AMD1 replacement of Table 94 (binding via
/// [V2G20-1204]) and [V2G20-1035] require the discharge powers to be <= 0.
///
/// The reactive-power limits are derived from the EVSE's reported reactive-power capability, reported as
/// non-negative absorption and non-positive injection. The grid limits are derived from the EVSE's nominal
/// voltage and frequency. Pure and session-free.
iso15118::d20::SaeDerTransferLimits build_sae_der_transfer_limits(const iso15118::d20::AcTransferLimits& ac_limits,
                                                                  std::optional<float> evse_max_reactive_power,
                                                                  std::uint32_t nominal_voltage);

/// Advertised voltage window, as a fraction of the advertised nominal voltage. These stay in step with the
/// first breakpoint of the library's default over- and under-voltage must-trip curves, which carry
/// PercentageV values the EV denormalizes against a base voltage. Per Table M.45 that base is usually but
/// not always the nominal voltage, and volt-var may adjust it autonomously.
constexpr float OVER_VOLTAGE_TRIP_FRACTION = 1.10f;
constexpr float UNDER_VOLTAGE_TRIP_FRACTION = 0.88f;

/// \brief Outcome of the AC_DER_SAE half of a derivation.
enum class SaeDerStatus {
    NotRequested,
    GridParametersMissing,
    Ready,
};

/// \brief One derivation pass: the limits built, plus the grid values they were built from.
struct DerLimitsDerivation {
    std::optional<iso15118::d20::IecDerTransferLimits> iec_limits;
    std::optional<iso15118::d20::SaeDerTransferLimits> sae_limits;
    std::optional<iso15118::d20::DerSaeSetupConfig> sae_setup_config;
    SaeDerStatus sae_status{SaeDerStatus::NotRequested};
    float nominal_frequency{0.0f};
    std::uint32_t nominal_voltage{0};
};

/// \brief Derive the DER transfer limits for the currently advertised energy services.
///
/// Limits are only built for the DER services present in \p services. AC_DER_SAE additionally needs a
/// positive nominal frequency and a positive nominal voltage: the voltage is the base for the PercentageV
/// grid code curves, and the frequency is the mandatory GridNominalFrequency ([V2G20-3269], Table M.41)
/// that the EV adopts in place of EVSENominalFrequency ([V2G20-3233]). Either one missing yields no SAE
/// limits and the GridParametersMissing status instead of a placeholder grid; neither has a fallback,
/// because a guess advertises a grid code that does not match the site. The frequency-trip curves are in
/// absolute Hz and are not scaled by the nominal frequency, so a 60 Hz site needs 60 Hz curve values
/// supplied with the setup config. Pure and session-free; the caller decides what to log and what to assign.
DerLimitsDerivation derive_der_limits(const std::vector<iso15118::message_20::datatypes::ServiceCategory>& services,
                                      const iso15118::d20::AcTransferLimits& ac_limits,
                                      std::optional<float> evse_max_reactive_power,
                                      std::optional<std::uint32_t> nominal_voltage);

/// \brief The DER limits currently assigned to the library setup config.
struct DerAppliedState {
    std::optional<iso15118::d20::IecDerTransferLimits> iec_limits;
    std::optional<iso15118::d20::SaeDerTransferLimits> sae_limits;
    std::optional<iso15118::d20::DerSaeSetupConfig> sae_setup_config;
};

/// \brief What an assignment did to the SAE limits, so the caller can pick the right log.
enum class DerSaeApplyTransition {
    Assigned,     ///< A freshly derived SAE set replaced whatever was assigned before.
    KeptPrevious, ///< Nothing was derived, so a previously derived SAE set stays in effect.
    NeverDerived, ///< Nothing was derived and nothing was ever assigned.
};

/// \brief What an assignment did, per flavor.
struct DerApplyTransitions {
    bool iec_assigned{false};
    DerSaeApplyTransition sae{DerSaeApplyTransition::NeverDerived};
};

/// \brief Assign a derivation onto the currently applied DER limits.
///
/// Only positive results are assigned: a service that is no longer advertised is not read by the library,
/// and withheld SAE limits must not discard a previously derived set. Pure apart from mutating \p current,
/// so the caller owns all logging and drives it from the returned transitions.
DerApplyTransitions apply_derivation(const DerLimitsDerivation& derived, DerAppliedState& current);

/// \brief Map an AC_DER_IEC EV's reported DER limits onto the OCPP-bound DERChargingParameters.
///
/// Surfaces the EV's reactive-power limits and session total discharge energy available. Active
/// charge/discharge power has no DERChargingParameters counterpart and is omitted. Table L.5 carries no
/// DER-function bitmap at all: in the IEC flow the SECC declares the functions it demands via
/// DERControlFunctions in the service parameter set ([V2G20-3190]) and the EV signals support by selecting
/// the service ([V2G20-3191]), so there is nothing EV-authored to map and the field is left unset. The SAE
/// flow differs; there the EV does send SupportedModes at ChargeParameterDiscovery ([V2G20-3409]).
/// Pure and session-free.
types::iso15118::DERChargingParameters
to_der_charging_parameters(const iso15118::message_20::datatypes::DER_AC_CPDReqEnergyTransferMode& ev);

/// \brief Map an AC_DER_SAE EV's ChargeParameterDiscovery request onto the OCPP-bound DERChargingParameters.
///
/// ev_supported_dercontrol comes from the request's SupportedModes bitmap ([V2G20-3409]); each SAE
/// function bit maps onto the matching grid_support DirectiveType. Bits without a counterpart are
/// dropped: the never-enableable ones (charge, discharge, charge loop target powers), which every
/// conforming EV sets, are logged at debug; the rest (constant watt, under frequency may trip) at
/// info. Reserved bits outside the SAE bitmap are warned about and ignored. The four excitation
/// fields and the optional session total discharge energy available pass through one-to-one.
/// The reactive-power limits are not mapped: SAE var absorption/injection semantics differ from
/// the IEC charge/discharge reactive fields.
types::iso15118::DERChargingParameters
to_der_charging_parameters(const iso15118::message_20::datatypes::sae::DER_SAE_AC_CPDReqEnergyTransferMode& ev);

/// \brief Map the EV's negotiated DER control-function bitset onto the grid_support DirectiveTypes it
/// supports, for DERChargingParameters.ev_supported_dercontrol.
///
/// The bitset is indexed by iso15118::iec::DERControlName. Over- and UnderFrequencyWattMode both map
/// to FreqWatt and are deduplicated. Returns nullopt when no control function is selected
/// (ev_supported_dercontrol has minItems:1, so the empty case must be unset, not an empty list).
std::optional<std::vector<types::grid_support::DirectiveType>>
map_ev_supported_der_controls(const std::bitset<12>& selected);

} // namespace module
