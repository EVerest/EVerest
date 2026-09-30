// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <generated/interfaces/external_energy_limits/Interface.hpp>

#include <optional>
#include <string>

namespace external_energy_limits {

/// \brief Checks if \p r_evse_energy_sink vector contains an element that has a mapping to the given \p evse_id
/// \param r_evse_energy_sink
/// \param evse_id
/// \return
bool is_evse_sink_configured(const std::vector<std::unique_ptr<external_energy_limitsIntf>>& r_evse_energy_sink,
                             const int32_t evse_id);

/// \brief Returns the reference of external_energy_limitsIntf in \p r_evse_energy_sink that maps to the given \p
/// evse_id \param r_evse_energy_sink \param evse_id \return
external_energy_limitsIntf&
get_evse_sink_by_evse_id(const std::vector<std::unique_ptr<external_energy_limitsIntf>>& r_evse_energy_sink,
                         const int32_t evse_id);

/// \brief Sets the current limit [A] of an OCPP 2.x charging schedule period on \p limits.
/// \p limit applies per phase. Where both \p limit_L2 and \p limit_L3 are given it is the one
/// for L1 (OCPP 2.1) and ac_max_current_A is the highest of the three, so that it does not cut
/// the others down; a lone \p limit_L2 or \p limit_L3 lowers its own phase only. Per phase
/// limits go into ac_max_current_per_phase_A.
void set_current_limit(types::energy::LimitsReq& limits, float limit, const std::optional<float>& limit_L2,
                       const std::optional<float>& limit_L3, const std::string& source);

/// \brief Sets the power limit [W] of an OCPP 2.x charging schedule period on \p limits.
/// \p limit is the sum over all phases, unless both \p limit_L2 and \p limit_L3 are given; then
/// it is the one for L1 (OCPP 2.1). Per phase limits go into ac_max_power_per_phase_W. The
/// total_power_W is what a symmetric load may draw within every phase, for consumers that do
/// not read the per phase limits.
void set_power_limit(types::energy::LimitsReq& limits, float limit, const std::optional<float>& limit_L2,
                     const std::optional<float>& limit_L3, const std::string& source);

} // namespace external_energy_limits
