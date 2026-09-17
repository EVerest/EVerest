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
} // namespace constants
