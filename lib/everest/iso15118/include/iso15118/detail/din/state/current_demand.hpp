// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/din/config.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message_din/current_demand.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

// Maps a CurrentDemandReq onto the -20 scheduled DC control mode the module-facing dc_charge_loop_req
// feedback speaks: the EV setpoint (target voltage/current) plus whichever of the three per-loop maxima
// the EV sent. Each maximum is optional on its own in the DIN CurrentDemandReq schema, so they are
// forwarded independently -- an EV sending only EVMaximumVoltageLimit/EVMaximumCurrentLimit and no
// EVMaximumPowerLimit must not lose the two it did send (EvseV2G publishes them per isUsed flag).
message_20::datatypes::Scheduled_DC_CLReqControlMode build_ev_setpoint(const message_din::CurrentDemandRequest& req);

message_din::CurrentDemandResponse handle_request(const message_din::CurrentDemandRequest& req,
                                                  const SessionConfig& config, float present_voltage,
                                                  float present_current, const dt::SessionId& session_id,
                                                  bool charger_stop = false,
                                                  std::optional<dt::DcEvseStatusCode> error_status_code = std::nullopt);

} // namespace iso15118::din::state
