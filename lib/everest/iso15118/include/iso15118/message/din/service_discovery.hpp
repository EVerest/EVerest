// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/din/msg_data_types.hpp>

#include <array>
#include <ctype.h>
#include <everest/util/vector/fixed_vector.hpp>
#include <optional>
#include <string>
#include <vector>

namespace iso15118::din::msg {

namespace data_types {

enum PaymentOption {
    Contract,
    ExternalPayment,
};
using PaymentOptions = std::vector<PaymentOption>;

enum EvseSupportedEnergyTransfer {
    AC_single_phase_core,
    AC_three_phase_core,
    DC_core,
    DC_extended,
    DC_combo_core,
    DC_dual,
    AC_core1p_DC_extended,
    AC_single_DC_core,
    AC_single_phase_three_phase_core_DC_extended,
    AC_core3p_DC_extended
};

struct ChargeService : Service {
    EvseSupportedEnergyTransfer energy_transfer_type;
};
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
};

} // namespace iso15118::din::msg