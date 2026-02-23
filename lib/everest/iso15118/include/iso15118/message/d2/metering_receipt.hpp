// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/d2/msg_data_types.hpp>

#include <optional>
#include <string>
#include <variant>

namespace iso15118::d2::msg {

struct MeteringReceiptRequest {
    Header header;
    std::optional<std::string> id;
    data_types::SESSION_ID session_id;
    std::optional<data_types::SAScheduleTupleID> sa_schedule_tuple_id;
    data_types::MeterInfo meter_info;
};

struct MeteringReceiptResponse {
    Header header;
    data_types::ResponseCode response_code;
    std::variant<data_types::AcEvseStatus, data_types::DcEvseStatus> evse_status;
};

} // namespace iso15118::d2::msg
