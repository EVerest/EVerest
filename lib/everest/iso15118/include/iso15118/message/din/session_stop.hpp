// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/din/msg_data_types.hpp>

namespace iso15118::din::msg {

struct SessionStopRequest {
    Header header;
};

struct SessionStopResponse {
    Header header;
    data_types::ResponseCode response_code;
};

} // namespace iso15118::din::msg
