// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d20/timeouts.hpp>

namespace iso15118::ev::d20::timeouts {

std::chrono::milliseconds response_timeout(StateID state) {
    switch (state) {
    case StateID::ServiceDetail:
        return MESSAGE_SERVICE_DETAIL;
    case StateID::AC_ChargeLoop:
    case StateID::AC_DER_IEC_ChargeLoop:
    case StateID::DC_ChargeLoop:
        return MESSAGE_CHARGE_LOOP;
    default:
        return MESSAGE;
    }
}

std::optional<std::chrono::milliseconds> ongoing_timeout(StateID state,
                                                         std::chrono::milliseconds authorization_override) {
    switch (state) {
    case StateID::Authorization:
        return (authorization_override.count() > 0) ? authorization_override : ONGOING_AUTHORIZATION;
    case StateID::AC_DER_SAE_ChargeParameterDiscovery:
        return ONGOING_CHARGE_PARAMETER_DISCOVERY;
    case StateID::ScheduleExchange:
        return ONGOING_SCHEDULE_EXCHANGE;
    case StateID::DC_CableCheck:
        return ONGOING_CABLE_CHECK;
    case StateID::DC_PreCharge:
        return ONGOING_PRE_CHARGE;
    case StateID::DC_WeldingDetection:
        return ONGOING_WELDING_DETECTION;
    default:
        return std::nullopt;
    }
}

} // namespace iso15118::ev::d20::timeouts
