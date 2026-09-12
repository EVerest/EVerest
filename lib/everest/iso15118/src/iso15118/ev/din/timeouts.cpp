// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/timeouts.hpp>

namespace iso15118::ev::din::timeouts {

std::chrono::milliseconds response_timeout(StateID state) {
    return state == StateID::CurrentDemand ? MESSAGE_CURRENT_DEMAND : MESSAGE;
}

std::optional<std::chrono::milliseconds> ongoing_timeout(StateID state) {
    switch (state) {
    case StateID::ContractAuthentication:
        return ONGOING_CONTRACT_AUTHENTICATION;
    case StateID::ChargeParameterDiscovery:
        return ONGOING_CHARGE_PARAMETER_DISCOVERY;
    case StateID::CableCheck:
        return ONGOING_CABLE_CHECK;
    case StateID::PreCharge:
        return ONGOING_PRE_CHARGE;
    case StateID::WeldingDetection:
        return ONGOING_WELDING_DETECTION;
    default:
        return std::nullopt;
    }
}

} // namespace iso15118::ev::din::timeouts
