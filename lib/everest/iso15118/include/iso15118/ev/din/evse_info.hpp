// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <iso15118/session/feedback.hpp>

namespace iso15118::ev::din {

// What the SECC told the EV during the handshake.
struct EvseInfo {
    std::string evse_id;
    // ServiceDiscoveryRes
    uint16_t charge_service_id{0};
    // ChargeParameterDiscoveryRes, merged with the CurrentDemandRes limits.
    std::optional<session::feedback::DcMaximumLimits> dc_present_limits{std::nullopt};
};

} // namespace iso15118::ev::din
