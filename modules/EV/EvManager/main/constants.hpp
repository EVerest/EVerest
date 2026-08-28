// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

namespace constants {
static constexpr auto THREE_PHASES{"3"};
static constexpr auto DEFAULT_LOOP_INTERVAL_MS{250};
static constexpr auto AC{"ac"};
static constexpr auto DC{"dc"};
// How long a requested stop may hold the pilot in C while waiting for the V2G session to
// wind down (PowerDelivery(stop) + SessionStop take well under 2 s when the link is healthy).
static constexpr auto STOP_HOLD_BUDGET_MS{10000};
} // namespace constants
