// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "energy_transfer_modes.hpp"

#include <algorithm>
#include <utility>

namespace module {

std::vector<types::iso15118::EnergyTransferMode>
get_supported_ac_energy_transfers(const types::evse_board_support::HardwareCapabilities& caps,
                                  bool supported_iso_ac_bpt, bool der_available) {
    std::vector<types::iso15118::EnergyTransferMode> energy_transfers;

    const auto min_phases = std::clamp(caps.min_phase_count_import, 1, 3);
    const auto max_phases = std::clamp(caps.max_phase_count_import, min_phases, 3);

    for (const auto& [count, mode] : {
             std::pair{1, types::iso15118::EnergyTransferMode::AC_single_phase_core},
             std::pair{2, types::iso15118::EnergyTransferMode::AC_two_phase},
             std::pair{3, types::iso15118::EnergyTransferMode::AC_three_phase_core},
         }) {
        if (count >= min_phases and count <= max_phases) {
            energy_transfers.push_back(mode);
        }
    }

    const bool export_capable = caps.max_current_A_export > 0 and caps.max_phase_count_export >= 1;
    if (supported_iso_ac_bpt and export_capable) {
        energy_transfers.push_back(types::iso15118::EnergyTransferMode::AC_BPT);
    }
    if (der_available and export_capable) {
        energy_transfers.push_back(types::iso15118::EnergyTransferMode::AC_DER_IEC);
    }
    return energy_transfers;
}

} // namespace module
