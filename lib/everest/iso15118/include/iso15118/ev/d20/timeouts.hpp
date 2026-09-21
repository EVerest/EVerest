// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include <iso15118/ev/d20/states.hpp>

// ISO 15118-20 EVCC timeouts ([V2G20-1592] ff.).
namespace iso15118::ev::d20::timeouts {

using namespace std::chrono_literals;

constexpr auto MESSAGE = 2000ms;
constexpr auto MESSAGE_SERVICE_DETAIL = 5000ms;
constexpr auto MESSAGE_CHARGE_LOOP = 500ms;

// Guards for the Processing::Ongoing polling loops. V2G_EVCC_Ongoing_Timeout is 60 s and applies
// to any response carrying EVSEProcessing=Ongoing [V2G20-710], [V2G20-2101]; the cable-check and
// pre-charge values are the DC-specific overrides V2G_EVCC_DC_CableCheck_Timeout and
// V2G_EVCC_DC_PreCharge_Timeout.
constexpr auto ONGOING_AUTHORIZATION = 60000ms;
constexpr auto ONGOING_SCHEDULE_EXCHANGE = 60000ms;
constexpr auto ONGOING_CABLE_CHECK = 40000ms;
constexpr auto ONGOING_PRE_CHARGE = 10000ms;
constexpr auto ONGOING_WELDING_DETECTION = 60000ms;

constexpr auto SDP_RESEND_INTERVAL = 250ms;
// SDP_max_request [V2G2-161], [V2G-DC-849].
constexpr uint32_t SDP_MAX_REQUESTS = 50;

// Response watchdog for a request sent from @p state.
std::chrono::milliseconds response_timeout(StateID state);

// Overall bound while @p state re-polls on Processing::Ongoing; nullopt for states without one.
// A non-zero @p authorization_override replaces the table value for Authorization, whose loop also
// carries the retries after a declined attempt and is the one an operator may want to lengthen.
std::optional<std::chrono::milliseconds> ongoing_timeout(StateID state,
                                                         std::chrono::milliseconds authorization_override = {});

} // namespace iso15118::ev::d20::timeouts
