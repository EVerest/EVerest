// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <map>
#include <optional>

#include <generated/types/grid_support.hpp>
#include <iso15118/d20/der_functions.hpp>

namespace module {

/// \brief Map the active grid_support DER directives onto AC_DER_IEC control functions.
///
/// Relays the three curve-family directive types that AC_DER_IEC models: VoltVar -> VoltVarMode,
/// WattVar -> WattVarMode, WattPF -> WattCosPhiMode. Percentage curve values are de-normalized to
/// absolute SI units against the EVSE bases (voltage, active power, reactive power); the WattPF y-axis
/// is a power factor and carries no base, mapping instead to a setpoint magnitude plus excitation sign.
/// Directives whose type has no AC_DER_IEC counterpart are skipped. Session-free; logs on rejected or
/// adjusted inputs. \p var_base is the EVSE reactive-power base; when absent, curves whose values are
/// expressed relative to reactive power are skipped.
std::map<iso15118::iec::DERControlName, iso15118::iec::DERControlFunction>
map_active_directives_to_der_functions(const types::grid_support::ActiveDirectiveSet& directives, float volt_base,
                                       float watt_base, std::optional<float> var_base);

} // namespace module
