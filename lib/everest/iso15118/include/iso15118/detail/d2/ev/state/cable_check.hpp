// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/cable_check.hpp>

namespace iso15118::d2::ev::state {

namespace dt = message_2::datatypes;

namespace cable_check {

message_2::CableCheckRequest create_request(const dt::DC_EVStatus& dc_ev_status);

enum class Action {
    Failed,
    Done,
    Retry,
};

struct Result {
    Action action{Action::Retry};
    // Valid only when action == Done.
    bool evse_ready{false};
    bool isolation_ok{false};
};

Result handle_response(const message_2::CableCheckResponse& res);

} // namespace cable_check

} // namespace iso15118::d2::ev::state
