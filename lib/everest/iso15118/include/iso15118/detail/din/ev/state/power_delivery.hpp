// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/power_delivery.hpp>

namespace iso15118::din::ev::state {

namespace dt = message_din::datatypes;

namespace power_delivery {

message_din::PowerDeliveryRequest create_request(bool ready_to_charge, const dt::DcEvStatus& dc_ev_status,
                                                 bool charging_complete);

struct Result {
    bool valid{false};
};

Result handle_response(const message_din::PowerDeliveryResponse& res);

} // namespace power_delivery

} // namespace iso15118::din::ev::state
