// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <generated/types/evse_board_support.hpp>
#include <generated/types/evse_manager.hpp>
#include <generated/types/iso15118.hpp>

namespace module {

std::vector<types::iso15118::EnergyTransferMode>
get_supported_ac_energy_transfers(const types::evse_board_support::HardwareCapabilities& caps,
                                  bool supported_iso_ac_bpt, bool der_available, const std::string& der_flavor);

/// Filters the energy modes based on the connector types, e.g.  with DC and DC_BPT replaced by MCS and MCS_BPT on an
/// MCS connector.
std::vector<types::iso15118::EnergyTransferMode>
filter_allowed_energy_transfers(const std::vector<types::iso15118::EnergyTransferMode>& allowed,
                                const std::optional<types::evse_manager::ConnectorTypeEnum>& connector_type);

} // namespace module
