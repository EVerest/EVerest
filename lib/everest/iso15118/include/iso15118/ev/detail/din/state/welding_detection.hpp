// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/welding_detection.hpp>

#include <iso15118/message_din/welding_detection.hpp>

namespace iso15118::ev::din::state {

namespace dt = message_din::datatypes;

namespace welding_detection {

using ev::welding_detection::Outcome;

message_din::WeldingDetectionRequest create_request(const dt::DcEvStatus& dc_ev_status);

} // namespace welding_detection

} // namespace iso15118::ev::din::state
