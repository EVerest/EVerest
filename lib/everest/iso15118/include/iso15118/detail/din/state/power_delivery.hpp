// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_din/power_delivery.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

// error_status_code is the module-reported EVSE error (send_error): it becomes the EVSEStatusCode
// [V2G-DC-638] and makes a ReadyToChargeState request fail with FAILED_PowerDeliveryNotApplied
// [V2G-DC-401].
message_din::PowerDeliveryResponse handle_request(const message_din::PowerDeliveryRequest& req,
                                                  const dt::SessionId& session_id, bool charger_stop = false,
                                                  std::optional<dt::DcEvseStatusCode> error_status_code = std::nullopt);

} // namespace iso15118::din::state
