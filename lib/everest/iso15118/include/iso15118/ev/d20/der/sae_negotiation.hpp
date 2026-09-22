// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

namespace iso15118::ev::d20::der {

// What AC_DER_SAE_ChargeParameterDiscovery settled with the SECC; the charge loop echoes it and
// keeps it current.
struct SaeNegotiation {
    // EnabledModes the EV echoes, AMD1 Table M.6 bit positions.
    std::uint32_t enabled_modes{0};
    bool permit_service{true};
    // EVUpdateTime, AMD1 Tables M.5 and M.8: microseconds of SECC time (8.3.3.3).
    std::uint64_t settings_update_time{0};
};

} // namespace iso15118::ev::d20::der
