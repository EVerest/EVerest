// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/cable_check.hpp>

namespace iso15118::ev::din::state {

namespace dt = message_din::datatypes;

namespace cable_check {

message_din::CableCheckRequest create_request(const dt::DcEvStatus& dc_ev_status);

} // namespace cable_check

} // namespace iso15118::ev::din::state
