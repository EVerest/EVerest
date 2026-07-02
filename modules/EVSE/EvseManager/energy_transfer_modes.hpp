// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <vector>

#include <generated/types/evse_board_support.hpp>
#include <generated/types/iso15118.hpp>

namespace module {

std::vector<types::iso15118::EnergyTransferMode>
get_supported_ac_energy_transfers(const types::evse_board_support::HardwareCapabilities& caps,
                                  bool supported_iso_ac_bpt, bool der_available);

} // namespace module
