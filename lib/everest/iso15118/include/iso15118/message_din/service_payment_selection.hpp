// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <vector>

#include "common_types.hpp"

namespace iso15118::message_din {

namespace datatypes {

struct SelectedService {
    uint16_t service_id{0};
    std::optional<int16_t> parameter_set_id;
};

} // namespace datatypes

struct ServicePaymentSelectionRequest {
    Header header;
    datatypes::PaymentOption selected_payment_option{datatypes::PaymentOption::ExternalPayment};
    std::vector<datatypes::SelectedService> selected_service_list;
};

struct ServicePaymentSelectionResponse {
    Header header;
    datatypes::ResponseCode response_code;
};

} // namespace iso15118::message_din
