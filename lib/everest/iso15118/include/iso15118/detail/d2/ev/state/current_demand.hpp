// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/message_2/current_demand.hpp>

namespace iso15118::d2::ev::state {

namespace dt = message_2::datatypes;

namespace current_demand {

// Inputs for the CurrentDemand request.
struct RequestParams {
    uint8_t present_soc{0};
    float target_voltage{0.0f};
    float target_current{0.0f};
    float max_current{0.0f};
    std::optional<float> max_power{std::nullopt};
};

message_2::CurrentDemandRequest create_request(const RequestParams& params);

struct Result {
    bool valid{false};
    std::optional<dt::EVSENotification> notification{std::nullopt};
    // The charger requested a stop: EVSENotification StopCharging or a non-ready DC_EVSEStatus status code
    // (Shutdown/EmergencyShutdown/Malfunction/NotReady).
    bool charger_requested_stop{false};
    // EVSENotification ReNegotiation or ReceiptRequired: EIM has no MeteringReceipt path, so terminate.
    bool renegotiation_or_receipt{false};
    float present_voltage{0.0f};
    float present_current{0.0f};
};

Result handle_response(const message_2::CurrentDemandResponse& res);

} // namespace current_demand

} // namespace iso15118::d2::ev::state
