// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/welding_detection.hpp>

#include <iso15118/message_2/welding_detection.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace welding_detection {

using ev::welding_detection::Outcome;

message_2::WeldingDetectionRequest create_request(const dt::DC_EVStatus& dc_ev_status);

} // namespace welding_detection

} // namespace iso15118::ev::d2::state
