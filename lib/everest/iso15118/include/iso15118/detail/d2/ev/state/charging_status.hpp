// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/message_2/charging_status.hpp>

namespace iso15118::d2::ev::state {

namespace dt = message_2::datatypes;

namespace charging_status {

message_2::ChargingStatusRequest create_request();

struct Result {
    bool valid{false};
    std::optional<dt::EVSENotification> notification{std::nullopt};
    // EVSENotification ReNegotiation or ReceiptRequired: EIM has no MeteringReceipt path, so terminate.
    bool renegotiation_or_receipt{false};
    std::optional<dt::PhysicalValue> evse_max_current{std::nullopt};
    uint8_t sa_schedule_tuple_id{0};
};

Result handle_response(const message_2::ChargingStatusResponse& res);

// Derives the SECC target power from the reported EVSE max current and the AC nominal voltage captured
// during ChargeParameterDiscovery (P = U * I). Returns an empty target when the current is absent.
d20::AcTargetPower compute_ac_target_power(const std::optional<dt::PhysicalValue>& evse_max_current,
                                           const std::optional<dt::PhysicalValue>& nominal_voltage);

} // namespace charging_status

} // namespace iso15118::d2::ev::state
