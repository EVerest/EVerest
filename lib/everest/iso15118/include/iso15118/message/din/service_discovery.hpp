// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/din/msg_data_types.hpp>

#include <everest/util/vector/fixed_vector.hpp>
#include <optional>
#include <vector>

namespace iso15118::din::msg {

namespace data_types {

enum class PaymentOption : uint8_t {
    Contract,
    ExternalPayment,
};
using PaymentOptions = everest::lib::util::fixed_vector<PaymentOption, din_paymentOptionType_2_ARRAY_SIZE>;

enum class EvseSupportedEnergyTransfer : uint8_t {
    AC_Single_Phase_Core,
    AC_Three_Phase_Core,
    DC_Core,
    DC_Extended,
    DC_Combo_Core,
    DC_Dual,
    AC_Core1p_DC_Extended,
    AC_Single_DC_Core,
    AC_Single_Phase_Three_Phase_Core_DC_extended,
    AC_Core3p_DC_Extended
};

struct ChargeService : Service {
    EvseSupportedEnergyTransfer energy_transfer_type;
};

using ServiceTagList = everest::lib::util::fixed_vector<Service, 1>; // Size is bound to 1 in libcbv2g
} // namespace data_types

struct ServiceDiscoveryRequest {
    Header header;
    std::optional<data_types::ServiceScope> service_scope{std::nullopt};
    std::optional<data_types::ServiceCategory> service_category{std::nullopt};
};

struct ServiceDiscoveryResponse {
    Header header;
    data_types::ResponseCode response_code;
    data_types::PaymentOptions payment_option_list;
    data_types::ChargeService charge_service;
    std::optional<data_types::ServiceTagList> service_list;
};

} // namespace iso15118::din::msg
