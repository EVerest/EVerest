// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include "common_types.hpp"

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::message_2 {

namespace datatypes {

struct SelectedService {
    uint16_t service_id;
    std::optional<int16_t> parameter_set_id;
};

using SelectedServiceList = everest::lib::util::fixed_vector<SelectedService, 16>;

} // namespace datatypes

struct PaymentServiceSelectionRequest {
    Header header;
    datatypes::PaymentOption selected_payment_option;
    datatypes::SelectedServiceList selected_service_list;
};

struct PaymentServiceSelectionResponse {
    Header header;
    datatypes::ResponseCode response_code;
};

} // namespace iso15118::message_2
