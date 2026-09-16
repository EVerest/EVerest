// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "common_types.hpp"

namespace iso15118::message_din {

namespace datatypes {

struct DcEvPowerDeliveryParameter {
    DcEvStatus dc_ev_status;
    std::optional<bool> bulk_charging_complete;
    bool charging_complete{false};
};

struct ProfileEntry {
    uint32_t charging_profile_entry_start{0}; // relative start time [s]
    int16_t charging_profile_entry_max_power{0};
};

struct ChargingProfile {
    int16_t sa_schedule_tuple_id{0};
    std::vector<ProfileEntry> profile_entries;
};

} // namespace datatypes

struct PowerDeliveryRequest {
    Header header;
    bool ready_to_charge_state{false};
    std::optional<datatypes::ChargingProfile> charging_profile;
    std::optional<datatypes::DcEvPowerDeliveryParameter> dc_ev_power_delivery_parameter;
};

struct PowerDeliveryResponse {
    Header header;
    datatypes::ResponseCode response_code;
    std::optional<datatypes::DcEvseStatus> dc_evse_status;
    std::optional<datatypes::AcEvseStatus> ac_evse_status;
};

} // namespace iso15118::message_din
