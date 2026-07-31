// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/pre_charge.hpp>

namespace iso15118::d2::ev::state {

namespace dt = message_2::datatypes;

namespace pre_charge {

message_2::PreChargeRequest create_request(const dt::DC_EVStatus& dc_ev_status, float present_voltage,
                                           float target_voltage);

struct Result {
    bool valid{false};
    // EVSE present voltage within the +/- 10 % band and the absolute voltage cap of the EV target.
    bool converged{false};
};

Result handle_response(const message_2::PreChargeResponse& res, float target_voltage);

} // namespace pre_charge

} // namespace iso15118::d2::ev::state
