// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

#include "common_types.hpp"

namespace iso15118::message_2 {

struct ChargingStatusRequest {
    Header header;
};

struct ChargingStatusResponse {
    Header header;
    datatypes::ResponseCode response_code;
    std::string evse_id;
    uint8_t sa_schedule_tuple_id;
    std::optional<datatypes::PhysicalValue> evse_max_current;
    std::optional<datatypes::MeterInfo> meter_info;
    std::optional<bool> receipt_required;
    datatypes::AC_EVSEStatus ac_evse_status;
};

} // namespace iso15118::message_2
