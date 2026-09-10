// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>

#include <iso15118/message_2/common_types.hpp>

#include <iso15118/ev/d2/pnc_config.hpp>

namespace iso15118::ev {

// ISO 15118-2 AC_EVChargeParameter values.
struct Iso2AcParams {
    float e_amount{60000.0f};
    float ev_max_voltage{400.0f};
    float ev_max_current{32.0f};
    float ev_min_current{10.0f};
};

// Parameters of the pre-20 engines (ISO 15118-2, DIN SPEC 70121). DC limits and targets come from
// the DcChargeParams monitor shared with the -20 engine.
struct EvSessionParams {
    // EVCCID of SessionSetupReq (the vehicle MAC).
    std::array<uint8_t, 6> evcc_mac{0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

    // Requested EnergyTransferMode; its AC/DC family selects the branch. DIN clamps to DC_extended.
    message_2::datatypes::EnergyTransferMode energy_transfer_mode{
        message_2::datatypes::EnergyTransferMode::DC_extended};

    Iso2AcParams ac{};

    d2::PnCConfig pnc{};
};

} // namespace iso15118::ev
