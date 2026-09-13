// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/message_2/current_demand.hpp>
#include <iso15118/session/feedback.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace current_demand {

message_2::CurrentDemandRequest create_request(const DcChargeParams& params);

struct Result {
    dt::EVSENotification notification{dt::EVSENotification::None};
    // EVSENotification StopCharging or a non-ready DC_EVSEStatus status code.
    bool charger_requested_stop{false};
    bool receipt_required{false};
};

Result handle_response(const message_2::CurrentDemandResponse& res);

// SECC limits carried by the response; nullopt when it advertises none.
std::optional<session::feedback::DcMaximumLimits> evse_present_limits(const message_2::CurrentDemandResponse& res);

} // namespace current_demand

} // namespace iso15118::ev::d2::state
