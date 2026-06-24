// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <vector>

#include <EvseManager.hpp>
#include <generated/types/evse_board_support.hpp>
#include <generated/types/iso15118.hpp>

namespace module::detail {

// Throwaway compile-time trigger for offering ISO15118-20 AC DER (IEC) to the EV. Flip to true to advertise it
// until the grid_support set_der_capability push lands and supplies this at runtime.
inline constexpr bool offer_iso_ac_der_iec = false;

std::vector<types::iso15118::EnergyTransferMode>
get_supported_ac_energy_transfers(const Conf& config, const types::evse_board_support::HardwareCapabilities& caps,
                                  bool offer_ac_der_iec = offer_iso_ac_der_iec);

} // namespace module::detail
