// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

// DC_EVStatus with the given ready flag, no error, and the module-reported SoC.
inline dt::DC_EVStatus make_dc_ev_status(const DcChargeParams& params, bool ev_ready) {
    dt::DC_EVStatus status;
    status.ev_ready = ev_ready;
    status.ev_error_code = dt::DC_EVErrorCode::NO_ERROR;
    status.ev_ress_soc = static_cast<int8_t>(params.present_soc);
    return status;
}

} // namespace iso15118::ev::d2::state
