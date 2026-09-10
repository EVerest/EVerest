// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/din/config.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message_din/current_demand.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

// Each maximum is optional on its own in the DIN CurrentDemandReq schema, so they are forwarded
// independently -- an EV sending two of the three must not lose them.
message_20::datatypes::Scheduled_DC_CLReqControlMode
build_ev_setpoint([[maybe_unused]] const message_din::CurrentDemandRequest& req);

message_din::CurrentDemandResponse handle_request([[maybe_unused]] const message_din::CurrentDemandRequest& req,
                                                  const SessionConfig& config, float present_voltage,
                                                  float present_current, const dt::SessionId& session_id,
                                                  bool charger_stop = false,
                                                  std::optional<dt::DcEvseStatusCode> error_status_code = std::nullopt);

} // namespace iso15118::din::state
