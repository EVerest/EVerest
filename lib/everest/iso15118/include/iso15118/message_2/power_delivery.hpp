// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include "common_types.hpp"

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::message_2 {

namespace datatypes {

struct ProfileEntry {
    uint32_t start;
    PhysicalValue max_power;
    std::optional<int8_t> max_number_of_phases_in_use;
};

struct ChargingProfile {
    everest::lib::util::fixed_vector<ProfileEntry, 24> profile_entry;
};

struct DC_EVPowerDeliveryParameter {
    DC_EVStatus dc_ev_status;
    std::optional<bool> bulk_charging_complete;
    bool charging_complete;
};

} // namespace datatypes

struct PowerDeliveryRequest {
    Header header;
    datatypes::ChargeProgress charge_progress;
    uint8_t sa_schedule_tuple_id;
    std::optional<datatypes::ChargingProfile> charging_profile;
    std::optional<datatypes::DC_EVPowerDeliveryParameter> dc_ev_power_delivery_parameter;
};

struct PowerDeliveryResponse {
    Header header;
    datatypes::ResponseCode response_code;
    std::optional<datatypes::AC_EVSEStatus> ac_evse_status;
    std::optional<datatypes::DC_EVSEStatus> dc_evse_status;
};

} // namespace iso15118::message_2
