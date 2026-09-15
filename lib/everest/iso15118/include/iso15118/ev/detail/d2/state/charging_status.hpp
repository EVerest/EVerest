// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/message_2/charging_status.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace charging_status {

message_2::ChargingStatusRequest create_request();

struct Result {
    dt::EVSENotification notification{dt::EVSENotification::None};
    bool receipt_required{false};
    std::optional<dt::PhysicalValue> evse_max_current{std::nullopt};
};

Result handle_response(const message_2::ChargingStatusResponse& res);

// SECC target power from the reported EVSE max current and the AC nominal voltage captured during
// ChargeParameterDiscovery (P = U * I, 230 V default). Empty when the current is absent.
iso15118::d20::AcTargetPower compute_ac_target_power(const std::optional<dt::PhysicalValue>& evse_max_current,
                                                     const std::optional<dt::PhysicalValue>& nominal_voltage);

} // namespace charging_status

} // namespace iso15118::ev::d2::state
