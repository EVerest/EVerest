// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <optional>

#include <iso15118/ev/din/states.hpp>

// DIN SPEC 70121 EVCC timeouts (Tables 75/77).
namespace iso15118::ev::din::timeouts {

using namespace std::chrono_literals;

constexpr auto MESSAGE = 2000ms;
// Table 75 V2G_EVCC_Msg_Timeout for CurrentDemandReq (Josev din_spec/timeouts.py).
constexpr auto MESSAGE_CURRENT_DEMAND = 500ms;

constexpr auto ONGOING_CONTRACT_AUTHENTICATION = 60000ms;
constexpr auto ONGOING_CHARGE_PARAMETER_DISCOVERY = 60000ms;
constexpr auto ONGOING_CABLE_CHECK = 40000ms;
constexpr auto ONGOING_PRE_CHARGE = 10000ms;
constexpr auto ONGOING_WELDING_DETECTION = 60000ms;

// EvseV2G MAX_RES_TIME parity.
constexpr auto MIN_REQUEST_INTERVAL = 100ms;

std::chrono::milliseconds response_timeout(StateID state);
std::optional<std::chrono::milliseconds> ongoing_timeout(StateID state);

} // namespace iso15118::ev::din::timeouts
