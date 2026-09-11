// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include "common_types.hpp"

namespace iso15118::message_2 {

struct SessionStopRequest {
    Header header;
    datatypes::ChargingSession charging_session;
};

struct SessionStopResponse {
    Header header;
    datatypes::ResponseCode response_code;
};

} // namespace iso15118::message_2
