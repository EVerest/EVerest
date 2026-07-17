// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include "common_types.hpp"

namespace iso15118::message_din {

struct SessionSetupRequest {
    Header header;
    datatypes::EvccId evcc_id;
};

struct SessionSetupResponse {
    Header header;
    datatypes::ResponseCode response_code;
    datatypes::EvseId evse_id;
    std::optional<int64_t> datetime_now;
};

} // namespace iso15118::message_din
