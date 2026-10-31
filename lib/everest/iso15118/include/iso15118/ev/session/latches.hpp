// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <variant>

#include <iso15118/ev/d20/control_event.hpp>

namespace iso15118::ev::session {

// Control events latched for the whole session. The Session owns one and every engine's Context
// refers to it, so a protocol handover keeps what the SAP phase latched.
struct Latches {
    bool stop{false};
    bool pause{false};
    bool cp_c_or_d{false};
};

// Stop and pause only ever latch on; the CP state follows the last report.
inline void latch(Latches& latches, const d20::ControlEvent& event) {
    if (const auto* stop = std::get_if<d20::StopCharging>(&event); stop != nullptr and *stop) {
        latches.stop = true;
    }
    if (const auto* pause = std::get_if<d20::PauseCharging>(&event); pause != nullptr and *pause) {
        latches.pause = true;
    }
    if (const auto* cp = std::get_if<d20::CpState>(&event)) {
        latches.cp_c_or_d = cp->c_or_d;
    }
}

} // namespace iso15118::ev::session
