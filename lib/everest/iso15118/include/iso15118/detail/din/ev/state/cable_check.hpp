// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/cable_check.hpp>

namespace iso15118::din::ev::state {

namespace dt = message_din::datatypes;

namespace cable_check {

message_din::CableCheckRequest create_request(const dt::DcEvStatus& dc_ev_status);

enum class Action {
    Failed,
    Done,
    Retry,
};

struct Result {
    Action action{Action::Retry};
    // Valid only when action == Done. [V2G-DC-893/894]: once EVSEProcessing is Finished the EVCC must
    // disregard EVSEIsolationStatus entirely and disregard all EVSEStatusCode values except EVSE_Shutdown
    // and EVSE_EmergencyShutdown before sending PreChargeReq; only those two stop the session.
    bool evse_shutdown{false};
};

Result handle_response(const message_din::CableCheckResponse& res);

} // namespace cable_check

} // namespace iso15118::din::ev::state
