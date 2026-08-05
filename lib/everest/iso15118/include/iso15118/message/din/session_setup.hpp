// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <vector>

#include <everest/util/vector/fixed_vector.hpp>
#include <iso15118/message/din/msg_data_types.hpp>

namespace iso15118::din::msg {

namespace data_types {
constexpr auto EVCC_ID_LENGTH = 6;
using EVCCID = everest::lib::util::fixed_vector<uint8_t, EVCC_ID_LENGTH>; // hexBinary, max length 6
constexpr auto EVSE_ID_LENGTH = 32;
using EVSEID = everest::lib::util::fixed_vector<uint8_t, EVSE_ID_LENGTH>; // hexBinary, max length 32
} // namespace data_types

struct SessionSetupRequest {
    Header header;
    data_types::EVCCID evcc_id;
};

struct SessionSetupResponse {
    Header header;
    data_types::ResponseCode response_code;
    data_types::EVSEID evse_id;
    std::optional<int64_t> date_time_now;
};

} // namespace iso15118::din::msg

