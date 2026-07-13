// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <vector>

#include <iso15118/message/din/msg_data_types.hpp>

namespace iso15118::din::msg {

namespace data_types {
struct SelectedService {
    ServiceID service_id;
    // TODO(kd): "In the scope of DIN SPEC 70121,
    // this optional element shall not be used."
    // Do we want to include the parameter_set_id regardless?
    // std::optional<ParameterSetID> parameter_set_id;
};

using SelectedServicesList = std::vector<SelectedService>;
// DIN 70121 specifies it as "unbounded" but:
// [V2G-DC-635] The number of SelectedService elements in the SelectedServiceListType shall be limited to 1.
constexpr auto SelectedServiceListMaxLength = 1;
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