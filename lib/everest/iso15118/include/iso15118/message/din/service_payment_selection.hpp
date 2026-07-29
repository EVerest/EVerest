// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <vector>

#include <everest/util/vector/fixed_vector.hpp>
#include <iso15118/message/din/msg_data_types.hpp>

namespace iso15118::din::msg {

namespace data_types {
using ParameterSetID = int16_t;
struct SelectedService {
    ServiceID service_id;
    std::optional<ParameterSetID> parameter_set_id;
};

// DIN 70121 specifies it as "unbounded" but:
// [V2G-DC-635] The number of SelectedService elements in the SelectedServiceListType shall be limited to 1.
using SelectedServicesList = everest::lib::util::fixed_vector<SelectedService, 1>;
} // namespace data_types

struct ServicePaymentSelectionRequest {
    Header header;
    data_types::PaymentOption selected_payment_option;
    data_types::SelectedServicesList selected_service_list;
};

struct ServicePaymentSelectionResponse {
    Header header;
    data_types::ResponseCode response_code;
};

} // namespace iso15118::din::msg
