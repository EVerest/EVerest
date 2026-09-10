// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/d20/control_event.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::d2 {

namespace dt = message_2::datatypes;

// What the module last told the SECC about the physical charger. These values change while a state
// is running -- they arrive as control events between two V2G messages -- so they can never be
// handed to a state through its constructor. Recorded centrally by the engine and read-only to the
// states, so no state can leave a stale or invented value behind for the next one; the two
// exceptions are named on the Context (set_contactor_closed, invalidate_cable_check).
struct EvseStatus {
    float present_voltage{0.0f};
    float present_current{0.0f};
    std::optional<dt::MeterInfo> latest_meter_info{};

    d20::EvseErrorCode active_error{d20::EvseErrorCode::None};
    std::optional<d20::IsolationStatus> reported_isolation_status{std::nullopt};
    bool emergency_shutdown{false};

    // The contactor state survives a renegotiation: it does not re-open, so no fresh ClosedContactor
    // confirmation arrives and PowerDelivery must answer from what it already knows.
    bool ac_contactor_closed{false};

    // Result of the physical isolation test. Invalidated when the contactor opens, because the
    // verified isolation no longer holds (ISO 15118-2 8.7.4.3 NOTE 1).
    bool cable_check_done{false};
    bool cable_check_fault{false};

    bool charger_stop_requested{false};
    d20::CpState current_cp_state{d20::CpState::A};
};

} // namespace iso15118::d2
