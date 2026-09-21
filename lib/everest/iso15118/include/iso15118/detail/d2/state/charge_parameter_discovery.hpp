// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/d2/config.hpp>
#include <iso15118/d2/states.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::d2::state {

// Returns the transition it causes: CableCheck (or PreChargeStart when no energy is available) on
// DC, AcPowerDelivery on AC, or back here while EVSEProcessing stays Ongoing.
Result process_charge_parameter_discovery(Context& ctx, const message_2::ChargeParameterDiscoveryRequest& req);

namespace dt = message_2::datatypes;

// One tuple, id 1, one PMaxSchedule entry. The horizon is the EV's requested DepartureTime when it
// sent one [V2G2-303], shortened while pausing for lack of energy; PMax is the hardware capability.
dt::SAScheduleList build_sa_schedule_list(const d2::SessionConfig& config, dt::EnergyTransferMode mode,
                                          std::optional<uint32_t> departure_time);

// [V2G2-366] reports the module error as the DC status code; the AC counterpart is the RCD flag.
message_2::ChargeParameterDiscoveryResponse
handle_request(const message_2::ChargeParameterDiscoveryRequest& req, const dt::SessionId& session_id,
               const d2::SessionConfig& config, bool charger_stop = false,
               std::optional<dt::DC_EVSEStatusCode> error_status_code = std::nullopt, bool rcd_error = false);

} // namespace iso15118::d2::state
