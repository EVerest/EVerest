// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <string>
#include <vector>

#include <generated/types/evse_board_support.hpp>
#include <generated/types/iso15118.hpp>

namespace module {

/// \brief Build the ISO 15118-20 AC energy transfer modes advertised for this EVSE.
///
/// Returns the phase-count modes covered by the reported import phase range, plus AC_BPT when
/// supported_iso_ac_bpt is set and the EVSE is export-capable, plus at most one AC DER mode when
/// der_available is set and the EVSE is export-capable. The two AC DER flavors are never returned together.
///
/// \p der_flavor accepts "NONE", "IEC" or "SAE" and is compared exactly, so "sae" is unrecognized. "NONE"
/// advertises no AC DER and warns, since der_available being set means something asked for a service that
/// cannot be offered. An unrecognized value advertises no AC DER and logs a warning naming it; the manifest
/// enum rejects any other value at config load, so that path is defensive.
std::vector<types::iso15118::EnergyTransferMode>
get_supported_ac_energy_transfers(const types::evse_board_support::HardwareCapabilities& caps,
                                  bool supported_iso_ac_bpt, bool der_available, const std::string& der_flavor);

} // namespace module
