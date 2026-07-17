// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

#include "common_types.hpp"

namespace iso15118::message_2 {

struct SessionSetupRequest {
    Header header;
    datatypes::EvccId evcc_id{};
};

struct SessionSetupResponse {
    Header header;
    datatypes::ResponseCode response_code;
    std::string evse_id;
    std::optional<int64_t> evse_timestamp;
};

} // namespace iso15118::message_2
