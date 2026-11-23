// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <generated/types/energy.hpp>

#include <everest/util/async/monitor.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>

namespace ocpp_multi {

/// \brief Values of the Current.Offered and Power.Offered measurands
struct OfferedLimits {
    std::optional<float> max_current_A;
    std::optional<float> max_power_W;
};

/// \brief An AC current limit usually comes without a power limit; the power is then the current limit on all
/// permitted phases at the supply voltage. \p supply_voltage_V is called only for that derivation.
OfferedLimits to_offered_limits(const types::energy::LimitsRes& limits, const std::function<float()>& supply_voltage_V);

/// \brief Offered limits by EVSE id. Enforced limits repeat about once a second and reading the supply voltage is a
/// database query, so the offered limits are derived only from limits that differ from the previous ones of the EVSE.
class OfferedLimitsCache {
public:
    /// \brief A changed supply voltage therefore applies with the next changed limit.
    /// \returns the offered limits when \p limits differ from the previous ones of \p evse_id
    std::optional<OfferedLimits> update(std::int32_t evse_id, const types::energy::LimitsRes& limits,
                                        const std::function<float()>& supply_voltage_V);
    OfferedLimits get(std::int32_t evse_id);
    void clear();

private:
    struct Entry {
        types::energy::LimitsRes limits;
        OfferedLimits offered;
    };
    everest::lib::util::monitor<std::map<std::int32_t, Entry>> m_entries;
};

} // namespace ocpp_multi
