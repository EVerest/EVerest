// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d2/context.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

// Builds a DC_EVSEStatus from the current context: the isolation status the context reports, no
// notification, given status code (EvseV2G parity).
inline dt::DC_EVSEStatus make_dc_evse_status(const Context& ctx, dt::DC_EVSEStatusCode status_code) {
    dt::DC_EVSEStatus status;
    status.notification = dt::EVSENotification::None;
    status.notification_max_delay = 0;
    status.isolation_status = ctx.isolation_level();
    status.status_code = status_code;
    return status;
}

// Builds an AC_EVSEStatus: no notification, RCD false (callers stamp an active RCD error at the respond site).
inline dt::AC_EVSEStatus make_ac_evse_status() {
    dt::AC_EVSEStatus status;
    status.notification = dt::EVSENotification::None;
    status.notification_max_delay = 0;
    status.rcd = false;
    return status;
}

// Overrides a DC status code with the module-reported EVSE error (Malfunction / UtilityInterruptEvent /
// EmergencyShutdown) when one is active, so the EV sees the fault [mirrors EvseV2G send_error].
inline void apply_evse_error(const Context& ctx, dt::DC_EVSEStatus& status) {
    if (const auto code = ctx.error_status_code()) {
        status.status_code = code.value();
    }
}

// Overrides the EVSEIsolationStatus of a DC response with the module's isolation-monitoring result
// (update_isolation_status) when it reported one, so the EV sees the real level -- above all No_IMD,
// which the response builders cannot derive on their own (mirrors EvseV2G, which reports the module
// value in every DC response). CableCheckRes applies the same precedence, but inside its own response
// builder (it has three legs with different fallbacks) rather than through this helper.
inline void apply_isolation_status(const Context& ctx, dt::DC_EVSEStatus& status) {
    if (const auto level = ctx.reported_isolation_level()) {
        status.isolation_status = level.value();
    }
}

// True for the DC energy transfer modes.
inline bool is_dc_mode(dt::EnergyTransferMode mode) {
    return mode == dt::EnergyTransferMode::DC_core or mode == dt::EnergyTransferMode::DC_extended or
           mode == dt::EnergyTransferMode::DC_combo_core or mode == dt::EnergyTransferMode::DC_unique;
}

} // namespace iso15118::d2::state
