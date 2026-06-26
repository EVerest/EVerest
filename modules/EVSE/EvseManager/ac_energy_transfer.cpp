// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ac_energy_transfer.hpp"

#include <algorithm>
#include <utility>

namespace module::detail {

std::vector<types::iso15118::EnergyTransferMode>
get_supported_ac_energy_transfers(const Conf& config, const types::evse_board_support::HardwareCapabilities& caps,
                                  bool offer_ac_der_iec) {
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

    if (config.supported_iso_ac_bpt and caps.max_current_A_export > 0 and caps.max_phase_count_export >= 1) {
        energy_transfers.push_back(types::iso15118::EnergyTransferMode::AC_BPT);
    }

    // TODO(ml): replace the offer_iso_ac_der_iec compile-time trigger with the grid_support set_der_capability
    // push once that lands.
    if (offer_ac_der_iec and caps.max_current_A_export > 0 and caps.max_phase_count_export >= 1) {
        energy_transfers.push_back(types::iso15118::EnergyTransferMode::AC_DER_IEC);
    }

    return energy_transfers;
}

} // namespace module::detail
