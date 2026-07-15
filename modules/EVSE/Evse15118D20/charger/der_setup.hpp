// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <bitset>
#include <optional>
#include <vector>

#include <generated/types/grid_support.hpp>
#include <generated/types/iso15118.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>

namespace module {

/// \brief Build the IEC DER transfer limits from the module's AC transfer limits.
///
/// Charge and discharge pass through from ac_limits (the discharge sign is already applied upstream).
/// nominal and max discharge are set equal, so the advertised pair satisfies V2G20-3229. When no
/// discharge limit is present the DER discharge limits are zero. Applying DER control directives to
/// the EV is handled separately by the DER control-function relay, not here.
iso15118::d20::IecDerTransferLimits build_iec_der_transfer_limits(const iso15118::d20::AcTransferLimits& ac_limits);

/// \brief Map an AC_DER_IEC EV's reported DER limits onto the OCPP-bound DERChargingParameters.
///
/// Surfaces the EV's reactive-power limits and session total discharge energy available. Active
/// charge/discharge power has no DERChargingParameters counterpart and is omitted; the EV's
/// supported-DER-control-functions bitmap is not present at ChargeParameterDiscovery (it lives in
/// ServiceDetail) and is left unset. Pure and session-free.
types::iso15118::DERChargingParameters
to_der_charging_parameters(const iso15118::message_20::datatypes::DER_AC_CPDReqEnergyTransferMode& ev);

/// \brief Map the EV's negotiated DER control-function bitset onto the grid_support DirectiveTypes it
/// supports, for DERChargingParameters.ev_supported_dercontrol.
///
/// The bitset is indexed by iso15118::iec::DERControlName. Over- and UnderFrequencyWattMode both map
/// to FreqWatt and are deduplicated. Returns nullopt when no control function is selected
/// (ev_supported_dercontrol has minItems:1, so the empty case must be unset, not an empty list).
std::optional<std::vector<types::grid_support::DirectiveType>>
map_ev_supported_der_controls(const std::bitset<12>& selected);

} // namespace module
