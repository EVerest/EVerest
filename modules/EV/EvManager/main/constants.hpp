// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

namespace constants {
static constexpr auto THREE_PHASES{"3"};
static constexpr auto DEFAULT_LOOP_INTERVAL_MS{250};
static constexpr auto AC{"ac"};
static constexpr auto DC{"dc"};
static constexpr auto AC_BPT{"ac_bpt"};
static constexpr auto AC_DER{"ac_der"};
static constexpr auto DC_BPT{"dc_bpt"};
// Megawatt Charging System: an ISO 15118-20 DC service of its own, not a DC variant, so it is
// requested as its own energy mode rather than through a DC fallback.
static constexpr auto MCS{"mcs"};
// How long a requested stop may hold the pilot in C while waiting for the V2G session to
// wind down (PowerDelivery(stop) + SessionStop take well under 2 s when the link is healthy).
static constexpr auto STOP_HOLD_BUDGET_MS{10000};
// How long the pilot stays in B after the stop hold releases before the script goes on. Without
// it a following unplug drops C straight to A, which the EVSE sees as an unplug, not a pause.
static constexpr auto STOP_DWELL_MS{750};
} // namespace constants
