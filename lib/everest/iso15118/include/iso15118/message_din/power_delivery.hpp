// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include "common_types.hpp"

namespace iso15118::message_din {

namespace datatypes {

struct DcEvPowerDeliveryParameter {
    DcEvStatus dc_ev_status;
    std::optional<bool> bulk_charging_complete;
    bool charging_complete{false};
};

} // namespace datatypes

struct PowerDeliveryRequest {
    Header header;
    bool ready_to_charge_state{false};
    std::optional<datatypes::DcEvPowerDeliveryParameter> dc_ev_power_delivery_parameter;
};

struct PowerDeliveryResponse {
    Header header;
    datatypes::ResponseCode response_code;
    std::optional<datatypes::DcEvseStatus> dc_evse_status;
    std::optional<datatypes::AcEvseStatus> ac_evse_status;
};

} // namespace iso15118::message_din
