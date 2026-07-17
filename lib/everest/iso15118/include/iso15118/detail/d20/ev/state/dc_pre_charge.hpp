// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/dc_pre_charge.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace dc_pre_charge {

message_20::DC_PreChargeRequest create_request(dt::Processing processing, const dt::RationalNumber& present_voltage,
                                               const dt::RationalNumber& target_voltage);

struct Result {
    bool valid{false};
    // EVSE present voltage within +/- 10 % of the EV target voltage.
    bool converged{false};
};

Result handle_response(const message_20::DC_PreChargeResponse& res, float target_voltage);

} // namespace dc_pre_charge

} // namespace iso15118::d20::ev::state
