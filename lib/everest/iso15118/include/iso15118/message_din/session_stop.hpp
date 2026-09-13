// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include "common_types.hpp"

namespace iso15118::message_din {

struct SessionStopRequest {
    Header header;
};

struct SessionStopResponse {
    Header header;
    datatypes::ResponseCode response_code;
};

} // namespace iso15118::message_din
