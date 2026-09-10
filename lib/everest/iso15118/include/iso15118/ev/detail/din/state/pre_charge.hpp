// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/pre_charge.hpp>

namespace iso15118::ev::din::state {

namespace dt = message_din::datatypes;

namespace pre_charge {

message_din::PreChargeRequest create_request(const dt::DcEvStatus& dc_ev_status, double target_voltage,
                                             double target_current);

// True when EVSEPresentVoltage is within +/- 10 % of the EV target voltage AND within an absolute
// 20 V cap. A non-positive target never converges.
// Deferred: [V2G-DC-909] wants convergence on the EV inlet measurement, which is not plumbed from the
// module; EVSEPresentVoltage is used instead.
bool converged(const message_din::PreChargeResponse& res, double target_voltage);

} // namespace pre_charge

} // namespace iso15118::ev::din::state
