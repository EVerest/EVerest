// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/d20/control_event.hpp>

namespace iso15118::din {

// What the module last told the SECC about the physical charger. These values change while a state
// is running -- they arrive as control events between two V2G messages -- so they can never be
// handed to a state through its constructor. Read-only to the states, so none can leave a stale or
// invented value behind for the next one; the present values and the cable-check result are
// reachable only through the named Context setters. DIN is DC-only, so unlike d2::EvseStatus there
// is no AC contactor state and no RCD flag.
struct EvseStatus {
    float present_voltage{0.0f};
    float present_current{0.0f};

    // Stamped into DC charge responses; EmergencyShutdown aborts the session (handled in the engine).
    d20::EvseErrorCode active_error{d20::EvseErrorCode::None};

    std::optional<d20::IsolationStatus> reported_isolation_status{std::nullopt};

    // Guarded by TIMEOUT_EMERGENCY_SHUTDOWN_GUARD so a silent EV cannot hold the connection open.
    bool emergency_shutdown{false};

    // A finished-but-failed cable check [V2G-DC-890]; distinct from "not finished yet".
    bool cable_check_done{false};
    bool cable_check_fault{false};

    // Latched by the engine in ANY state: every later status-carrying response tells the EV to stop.
    bool charger_stop_requested{false};
    // Set by the engine on the STOP_CHARGING guard timeout; respond() then fails every further response.
    bool charger_stop_ignored{false};

    // Initial A: only "== B" decisions are taken from it, and B is always an explicit report.
    d20::CpState current_cp_state{d20::CpState::A};
};

} // namespace iso15118::din
