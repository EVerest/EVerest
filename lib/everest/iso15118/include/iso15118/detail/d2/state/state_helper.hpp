// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d2/context.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

inline dt::DC_EVSEStatus make_dc_evse_status(const Context& ctx, dt::DC_EVSEStatusCode status_code) {
    dt::DC_EVSEStatus status;
    status.notification = dt::EVSENotification::None;
    status.notification_max_delay = 0;
    status.isolation_status = ctx.isolation_level();
    status.status_code = status_code;
    return status;
}

// Builds an AC_EVSEStatus: no notification, RCD false (callers stamp an active RCD error at the respond site).
// [V2G2-920]: the SECC measures CP state B within V2G_SECC_Msg_Performance_Time of a
// WeldingDetectionReq or SessionStopReq. The gate applies only once PowerDelivery(Stop) has opened
// the contactor [V2G2-913], and both nodes that can receive one test it before answering.
inline bool cp_state_b_outstanding(const Context& ctx) {
    return ctx.session().power_delivery_stopped and ctx.evse().current_cp_state != d20::CpState::B;
}

inline dt::AC_EVSEStatus make_ac_evse_status() {
    dt::AC_EVSEStatus status;
    status.notification = dt::EVSENotification::None;
    status.notification_max_delay = 0;
    status.rcd = false;
    return status;
}

// So the EV sees a module-reported fault (mirrors EvseV2G send_error).
inline void apply_evse_error(const Context& ctx, dt::DC_EVSEStatus& status) {
    if (const auto code = ctx.error_status_code()) {
        status.status_code = code.value();
    }
}

// So the EV sees the real level -- above all No_IMD, which the response builders cannot derive on
// their own. CableCheckRes applies the same precedence inside its own builder, which has three legs
// with different fallbacks, rather than through this helper.
inline void apply_isolation_status(const Context& ctx, dt::DC_EVSEStatus& status) {
    if (const auto level = ctx.reported_isolation_level()) {
        status.isolation_status = level.value();
    }
}

inline bool is_dc_mode(dt::EnergyTransferMode mode) {
    return mode == dt::EnergyTransferMode::DC_core or mode == dt::EnergyTransferMode::DC_extended or
           mode == dt::EnergyTransferMode::DC_combo_core or mode == dt::EnergyTransferMode::DC_unique;
}

} // namespace iso15118::d2::state
