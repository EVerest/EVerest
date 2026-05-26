// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/d2/msg_data_types.hpp>

#include <optional>

namespace iso15118::d2::msg {

struct ServiceDetailRequest {
    Header header;
    data_types::ServiceID service_id;
};

struct ServiceDetailResponse {
    Header header;
    data_types::ResponseCode response_code;
    data_types::ServiceID service_id;
    std::optional<data_types::ServiceParameterList> service_parameter_list{std::nullopt};
};

} // namespace iso15118::d2::msg
