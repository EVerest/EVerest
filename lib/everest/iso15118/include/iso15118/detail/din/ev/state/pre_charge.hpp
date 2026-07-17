// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/pre_charge.hpp>

namespace iso15118::din::ev::state {

namespace dt = message_din::datatypes;

namespace pre_charge {

message_din::PreChargeRequest create_request(const dt::DcEvStatus& dc_ev_status, double target_voltage,
                                             double target_current);

struct Result {
    bool valid{false};
    // EVSE present voltage within +/- 10 % of the EV target voltage AND within an absolute 20 V cap.
    bool converged{false};
};

Result handle_response(const message_din::PreChargeResponse& res, double target_voltage);

} // namespace pre_charge

} // namespace iso15118::din::ev::state
