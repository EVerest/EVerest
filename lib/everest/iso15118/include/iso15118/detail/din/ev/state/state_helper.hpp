// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/din/ev/context.hpp>
#include <iso15118/din/ev/state/session_stop.hpp>
#include <iso15118/message_din/common_types.hpp>

namespace iso15118::din::ev::state {

// Records a pending stop/pause when the corresponding control event is received during the handshake.
// The pending flag is checked at transition time (see stop_if_pending).
inline void handle_stop_control_event(Context& ctx) {
    if (const auto* stop = ctx.get_control_event<StopCharging>(); stop and static_cast<bool>(*stop)) {
        ctx.pending_stop = ChargingSession::Terminate;
    } else if (const auto* pause = ctx.get_control_event<PauseCharging>(); pause and static_cast<bool>(*pause)) {
        ctx.pending_stop = ChargingSession::Pause;
    }
}

// If a stop/pause is pending, returns a SessionStop state to divert to; otherwise nullptr.
inline BasePointerType stop_if_pending(Context& ctx) {
    if (ctx.pending_stop.has_value()) {
        return ctx.create_state<SessionStop>();
    }
    return nullptr;
}

// Updates the DC control-event cache from the current control event. Called on CONTROL_MESSAGE in the
// DC charging-loop states so create_request reads the latest set points.
inline void handle_dc_control_event(Context& ctx) {
    if (const auto* voltage_current = ctx.get_control_event<PresentVoltageCurrent>()) {
        ctx.dc_cache.present_voltage = voltage_current->voltage;
        ctx.dc_cache.present_current = voltage_current->current;
    } else if (const auto* targets = ctx.get_control_event<UpdateDcTargets>()) {
        ctx.dc_cache.target_voltage = targets->target_voltage;
        ctx.dc_cache.target_current = targets->target_current;
    } else if (const auto* soc = ctx.get_control_event<UpdateSoc>()) {
        ctx.dc_cache.present_soc = soc->soc;
    } else if (const auto* params = ctx.get_control_event<UpdateDcParameters>()) {
        if (params->target_energy_request.has_value()) {
            ctx.dc_cache.target_energy_request = params->target_energy_request;
        }
        if (params->max_energy_request.has_value()) {
            ctx.dc_cache.max_energy_request = params->max_energy_request;
        }
        if (params->min_energy_request.has_value()) {
            ctx.dc_cache.min_energy_request = params->min_energy_request;
        }
        if (params->max_charge_power.has_value()) {
            ctx.dc_cache.max_charge_power = params->max_charge_power;
        }
        if (params->max_charge_current.has_value()) {
            ctx.dc_cache.max_charge_current = params->max_charge_current;
        }
    }
}

// Builds a DC_EVStatus with the given ready flag, no error, and the cached present SoC.
inline message_din::datatypes::DcEvStatus make_dc_ev_status(const Context& ctx, bool ev_ready) {
    message_din::datatypes::DcEvStatus status;
    status.ev_ready = ev_ready;
    status.ev_error_code = message_din::datatypes::DcEvErrorCode::NO_ERROR;
    status.ev_ress_soc = static_cast<int8_t>(ctx.dc_cache.present_soc);
    return status;
}

} // namespace iso15118::din::ev::state

