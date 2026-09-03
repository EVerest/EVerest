// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>
#include <utility>
#include <vector>

#include <generated/types/grid_support.hpp>
#include <iso15118/d20/der_functions.hpp>

namespace module {

struct SaeRelayResult {
    /// Shipped default with the mapped directives applied on top.
    iso15118::sae::DERControl der_control;
    /// Directives that lost their Annex M slot to a higher-precedence directive.
    std::vector<std::string> shadowed_ids;
    /// Directive types with no Annex M target.
    std::vector<types::grid_support::DirectiveType> unmapped;
};

/// \brief Map the active grid_support DER directives onto an AC_DER_SAE (Annex M) DERControl.
///
/// Stateless: every call starts from get_default_sae_der_control(nominal_voltage_v), so a directive that
/// disappears from the set falls back to the shipped default. Lowest priority wins an Annex M element, array
/// order breaks ties, losers are reported in shadowed_ids. A winner that fails validation is rejected and
/// frees its element for the next candidate. This function owns every OCPP-to-Annex-M conversion; the
/// library below it knows nothing about OCPP.
///
/// Trip curves arrive in OCPP orientation (x = the threshold in percent of nominal voltage for the HV/LV
/// types or in Hz for the HF/LF types, y = the seconds the threshold may be exceeded) and are transposed
/// into Annex M orientation (x = duration in seconds, y = threshold) and sorted by duration; two points
/// sharing a duration reject the directive. HV/LV thresholds stay percentages of nominal (PercentageV), HF/LF
/// stay Hz; no volt conversion happens.
///
/// EnterService voltage bands are volts on both sides (AMD1 Table 1) and are copied through unchanged.
/// FixedVar.setpoint is negated into ConstantVar.var_setpoint because OCPP and Annex M disagree on the sign
/// of reactive-power injection. The grid_support directive priority is forwarded into every written element
/// that carries one (EnterService has none); values outside uint16 are omitted.
/// Curves with more than CurveDataPointsMaxLength points are rejected, never truncated: dropping points
/// silently changes the grid-code behavior the operator asked for.
///
/// Response-curve y_unit labels are translated to their Annex M counterparts and must belong to the unit
/// family the element expects. \p nominal_frequency_hz converts the FreqDroop thresholds to deadbands, and
/// \p nominal_voltage_v builds the default; when it is not positive every directive is rejected.
/// Single-phase only.
SaeRelayResult map_active_directives_to_sae_der_control(const types::grid_support::ActiveDirectiveSet& directives,
                                                        float nominal_voltage_v, float nominal_frequency_hz);

/// \brief Order-independent identity of a mapping input.
///
/// Two equal inputs yield the same DERControl, so a re-apply need not dictate a new grid code revision. A
/// re-received directive changes received_at, and a changed nominal changes the default the mapping starts
/// from as well as the FreqDroop deadbands.
struct SaeRelayInput {
    /// (id, received_at) of every directive in the set, sorted so array order does not matter.
    std::vector<std::pair<std::string, std::string>> directives;
    float nominal_voltage_v{0.0f};
    float nominal_frequency_hz{0.0f};

    bool operator==(const SaeRelayInput& other) const;
};

SaeRelayInput sae_relay_input(const types::grid_support::ActiveDirectiveSet& directives, float nominal_voltage_v,
                              float nominal_frequency_hz);

/// \brief Names of the Annex M functions left at their inert default (disabled or absent) in \p control.
std::vector<std::string> inert_sae_der_functions(const iso15118::sae::DERControl& control);

} // namespace module
