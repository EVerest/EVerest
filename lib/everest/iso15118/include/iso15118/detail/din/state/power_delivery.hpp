// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/din/states.hpp>
#include <iso15118/message_din/power_delivery.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

// Returns the transition: CurrentDemand on ReadyToChargeState=TRUE [V2G-DC-462], WeldingDetection on
// FALSE [V2G-DC-459].
//
// This is an action, not a wait state: section 9.7.4.2.4 never has the SECC waiting for a
// PowerDeliveryReq alone, so the pre-charge node and the charge loop both dispatch to it.
Result process_power_delivery(Context& ctx, const message_din::PowerDeliveryRequest& req);

// error_status_code becomes the EVSEStatusCode [V2G-DC-638] and makes a ReadyToChargeState request
// fail with FAILED_PowerDeliveryNotApplied [V2G-DC-401].
message_din::PowerDeliveryResponse handle_request(const message_din::PowerDeliveryRequest& req,
                                                  const dt::SessionId& session_id, bool charger_stop = false,
                                                  std::optional<dt::DcEvseStatusCode> error_status_code = std::nullopt);

} // namespace iso15118::din::state
