// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <everest/util/vector/fixed_vector.hpp>

#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::ev::d2 {

// What the SECC told the EV during the handshake.
struct EvseInfo {
    std::string evse_id;

    // ServiceDiscoveryRes
    uint16_t selected_charge_service_id{0};
    bool contract_offered{false};
    bool certificate_service_offered{false};

    // ChargeParameterDiscoveryRes
    uint8_t sa_schedule_tuple_id{0};
    everest::lib::util::fixed_vector<message_2::datatypes::PMaxScheduleEntry, 12> selected_pmax_schedule{};
    std::optional<message_2::datatypes::PhysicalValue> ac_nominal_voltage{std::nullopt};
};

} // namespace iso15118::ev::d2
