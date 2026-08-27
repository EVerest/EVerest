// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message/power_delivery.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace power_delivery {

// The EV never sends Standby (SECC returns WARNING_StandbyNotAllowed) [flow spec §PowerDelivery].
message_20::PowerDeliveryRequest create_request(dt::Progress charge_progress,
                                                std::optional<dt::ChannelSelection> channel_selection);

struct Result {
    bool valid{false};
};

Result handle_response(const message_20::PowerDeliveryResponse& res);

} // namespace power_delivery

} // namespace iso15118::d20::ev::state
