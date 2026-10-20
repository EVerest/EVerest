// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <generated/types/powermeter.hpp>
#include <nlohmann/json.hpp>

namespace module::main {

/// \brief Whether \p current is a live measurement the device has not delivered before \p last_published.
///
/// The DCBM 400/600 refreshes its live measurements about once per second, whatever the poll interval, so a
/// poll can return the sample of the previous poll again. Consumers of the powermeter var count publications
/// as measurements: EvseManager confirms the DC output disabled after two measurements below 5 A, and the HLC
/// stacks answer a WeldingDetectionReq with the second measurement received after it (IEC 61851-23:2023
/// CC.6.3). A sample published twice would pass for two measurements, so a repeated sample is not published
/// again. The whole dataset is compared, not only the device timestamp: a changed value with an unchanged
/// timestamp still counts as new, so a coarse timestamp cannot hide a measurement.
inline bool is_new_measurement(const std::optional<types::powermeter::Powermeter>& last_published,
                               const types::powermeter::Powermeter& current) {
    if (not last_published.has_value()) {
        return true;
    }
    return nlohmann::json(*last_published) != nlohmann::json(current);
}

} // namespace module::main
