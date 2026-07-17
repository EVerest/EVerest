// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d2/ev/context.hpp>
#include <iso15118/d2/ev/control_event.hpp>
#include <iso15118/d2/ev/state/session_stop.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::d2::ev::state {

// Records a pending stop/pause when the corresponding control event is received during the handshake.
// The pending flag is checked at transition time (see stop_if_pending).
inline void handle_stop_control_event(Context& ctx) {
    if (const auto* stop = ctx.get_control_event<StopCharging>(); stop and static_cast<bool>(*stop)) {
        ctx.pending_stop = message_2::datatypes::ChargingSession::Terminate;
    } else if (const auto* pause = ctx.get_control_event<PauseCharging>(); pause and static_cast<bool>(*pause)) {
        ctx.pending_stop = message_2::datatypes::ChargingSession::Pause;
    }
}

// If a stop/pause is pending, returns a SessionStop state to divert to; otherwise nullptr.
inline BasePointerType stop_if_pending(Context& ctx) {
    if (ctx.pending_stop.has_value()) {
        return ctx.create_state<SessionStop>();
    }
    return nullptr;
}

// True for the DC energy transfer modes.
inline bool is_dc_mode(message_2::datatypes::EnergyTransferMode mode) {
    using message_2::datatypes::EnergyTransferMode;
    return mode == EnergyTransferMode::DC_core or mode == EnergyTransferMode::DC_extended or
           mode == EnergyTransferMode::DC_combo_core or mode == EnergyTransferMode::DC_unique;
}

// True for the AC energy transfer modes.
inline bool is_ac_mode(message_2::datatypes::EnergyTransferMode mode) {
    using message_2::datatypes::EnergyTransferMode;
    return mode == EnergyTransferMode::AC_single_phase_core or mode == EnergyTransferMode::AC_three_phase_core;
}

// Updates the DC control-event cache in the context from the current control event. Called on
// CONTROL_MESSAGE in the DC charging-loop states so create_request reads the latest set points.
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
        // Only the parameters with a corresponding ISO 15118-2 wire field are cached and applied:
        // max_charge_power -> CurrentDemandReq/DC_EVChargeParameter.EVMaximumPowerLimit,
        // max_charge_current -> CurrentDemandReq/DC_EVChargeParameter.EVMaximumCurrentLimit,
        // max_energy_request -> ChargeParameterDiscoveryReq/DC_EVChargeParameter.EVEnergyRequest.
        // target_energy_request and min_energy_request have no ISO 15118-2
        // CurrentDemandReq/ChargeParameterDiscoveryReq representation and are intentionally dropped.
        if (params->max_charge_power.has_value()) {
            ctx.dc_cache.max_charge_power = params->max_charge_power;
        }
        if (params->max_charge_current.has_value()) {
            ctx.dc_cache.max_charge_current = params->max_charge_current;
        }
        if (params->max_energy_request.has_value()) {
            ctx.dc_cache.max_energy_request = params->max_energy_request;
        }
    }
}

// Builds a DC_EVStatus with the given ready flag, no error, and the cached present SoC.
inline message_2::datatypes::DC_EVStatus make_dc_ev_status(const Context& ctx, bool ev_ready) {
    message_2::datatypes::DC_EVStatus status;
    status.ev_ready = ev_ready;
    status.ev_error_code = message_2::datatypes::DC_EVErrorCode::NO_ERROR;
    status.ev_ress_soc = static_cast<int8_t>(ctx.dc_cache.present_soc);
    return status;
}

} // namespace iso15118::d2::ev::state
