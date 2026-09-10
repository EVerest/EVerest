// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/cable_check.hpp>

namespace iso15118::ev::din::state {

namespace dt = message_din::datatypes;

namespace cable_check {

message_din::CableCheckRequest create_request(const dt::DcEvStatus& dc_ev_status);

struct Result {
    // EVSEProcessing is Finished; otherwise the isolation test poll repeats.
    bool finished{false};
    // Valid only when finished. [V2G-DC-893/894]: once EVSEProcessing is Finished the EVCC disregards
    // EVSEIsolationStatus entirely and every EVSEStatusCode except EVSE_Shutdown and
    // EVSE_EmergencyShutdown before sending PreChargeReq.
    bool evse_shutdown{false};
};

Result handle_response(const message_din::CableCheckResponse& res);

} // namespace cable_check

} // namespace iso15118::ev::din::state
