// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/d2/msg_data_types.hpp>

namespace iso15118::d2::msg {

struct PaymentServiceSelectionRequest {
    Header header;
    data_types::PaymentOption selected_payment_option;
    data_types::SelectedServiceList selected_service_list;
};

struct PaymentServiceSelectionResponse {
    Header header;
    data_types::ResponseCode response_code;
};

} // namespace iso15118::d2::msg
