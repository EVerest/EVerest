// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/timeouts.hpp>

namespace iso15118::ev::d2::timeouts {

std::chrono::milliseconds response_timeout(StateID state) {
    switch (state) {
    case StateID::PowerDelivery:
        return MESSAGE_POWER_DELIVERY;
    case StateID::CurrentDemand:
        return MESSAGE_CURRENT_DEMAND;
    case StateID::CertificateInstallation:
        return MESSAGE_CERTIFICATE_INSTALLATION;
    default:
        return MESSAGE;
    }
}

std::optional<std::chrono::milliseconds> ongoing_timeout(StateID state) {
    switch (state) {
    case StateID::Authorization:
    case StateID::ChargeParameterDiscovery:
        return ONGOING;
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

} // namespace iso15118::ev::d2::timeouts
