// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/d2/config.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

// Builds the single advertised SAScheduleList (one tuple, id 1, one PMaxSchedule entry). The horizon is
// the EV's requested DepartureTime when it sent one [V2G2-303], shortened while pausing for lack of
// energy. PMax is the hardware capability for the requested transfer mode (DC power-supply maximum, or
// the nominal AC power across the mode's phase count).
dt::SAScheduleList build_sa_schedule_list(const d2::SessionConfig& config, dt::EnergyTransferMode mode,
                                          std::optional<uint32_t> departure_time);

// error_status_code / rcd_error carry the module-reported EVSE error (send_error) into the response:
// [V2G2-366] has the SECC report the Table 98 status codes in every DC response, and the AC_EVSEStatus RCD
// flag is mandatory in every AC one -- ChargeParameterDiscoveryRes included.
message_2::ChargeParameterDiscoveryResponse
handle_request(const message_2::ChargeParameterDiscoveryRequest& req, const dt::SessionId& session_id,
               const d2::SessionConfig& config, bool charger_stop = false,
               std::optional<dt::DC_EVSEStatusCode> error_status_code = std::nullopt, bool rcd_error = false);

} // namespace iso15118::d2::state
