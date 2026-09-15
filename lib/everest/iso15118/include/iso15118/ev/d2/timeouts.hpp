// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <optional>

#include <iso15118/ev/d2/states.hpp>

// ISO 15118-2 EVCC timeouts (Table 109).
namespace iso15118::ev::d2::timeouts {

using namespace std::chrono_literals;

constexpr auto MESSAGE = 2000ms;
constexpr auto MESSAGE_POWER_DELIVERY = 5000ms;
constexpr auto MESSAGE_CURRENT_DEMAND = 250ms;
// The SECC relays CertificateInstallation to a backend.
constexpr auto MESSAGE_CERTIFICATE_INSTALLATION = 5000ms;

constexpr auto ONGOING = 60000ms; // V2G_EVCC_Ongoing_Timeout (Authorization, ChargeParameterDiscovery)
constexpr auto ONGOING_CABLE_CHECK = 40000ms;
// Table 109: 7 s; relaxed to 10 s for slow power supplies (deliberate deviation).
constexpr auto ONGOING_PRE_CHARGE = 10000ms;
constexpr auto ONGOING_WELDING_DETECTION = 20000ms;

// EvseV2G MAX_RES_TIME parity: no request faster than this.
constexpr auto MIN_REQUEST_INTERVAL = 100ms;

std::chrono::milliseconds response_timeout(StateID state);
std::optional<std::chrono::milliseconds> ongoing_timeout(StateID state);

} // namespace iso15118::ev::d2::timeouts
