// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/welding_detection.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

message_2::WeldingDetectionResponse handle_request(const message_2::WeldingDetectionRequest& req,
                                                   const dt::SessionId& session_id, float present_voltage);

} // namespace iso15118::d2::state
