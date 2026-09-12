// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/message_din/common_types.hpp>

namespace iso15118::ev::din::state {

namespace dt = message_din::datatypes;

// DC_EVStatus with the given ready flag, no error and the module's present SoC.
inline dt::DcEvStatus make_dc_ev_status(const DcChargeParams& params, bool ev_ready) {
    dt::DcEvStatus status;
    status.ev_ready = ev_ready;
    status.ev_error_code = dt::DcEvErrorCode::NO_ERROR;
    status.ev_ress_soc = static_cast<int8_t>(params.present_soc);
    return status;
}

// [V2G-DC-625]: DIN SPEC 70121 offers DC_core and DC_extended only.
inline dt::EnergyTransferMode din_energy_transfer_mode(dt::EnergyTransferMode requested) {
    return (requested == dt::EnergyTransferMode::DC_core) ? dt::EnergyTransferMode::DC_core
                                                          : dt::EnergyTransferMode::DC_extended;
}

// DIN Table 73: ChargingComplete means the EV is fully charged, not that charging is ending.
inline bool charging_complete(const DcChargeParams& params) {
    return params.present_soc >= 100.0;
}

} // namespace iso15118::ev::din::state
