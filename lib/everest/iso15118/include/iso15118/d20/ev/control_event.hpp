// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <variant>

#include <iso15118/d20/control_event.hpp>

namespace iso15118::d20::ev {

// Re-use the direction-neutral control events from the SECC side where suitable.
using d20::PauseCharging;
using d20::PresentVoltageCurrent;
using d20::StopCharging;

// Update the DC target set point (charge loop / pre charge).
struct UpdateDcTargets {
    float target_voltage;
    float target_current;
};

// Update the present state of charge (0 - 100).
struct UpdateSoc {
    uint8_t soc;
};

// Update the dynamic DC charge parameters (energy request window).
struct UpdateDcParameters {
    std::optional<float> target_energy_request;
    std::optional<float> max_energy_request;
    std::optional<float> min_energy_request;
    std::optional<float> max_charge_power;
    std::optional<float> max_charge_current;
};

// Reported control pilot state (IEC 61851-1): true while the EV applies state C or D (S2 closed).
// The DC cable check states use this to hold the first CableCheckReq until the EV is in state C/D.
struct CpState {
    bool c_or_d{false};
};

using ControlEvent = std::variant<StopCharging, PauseCharging, PresentVoltageCurrent, UpdateDcTargets, UpdateSoc,
                                  UpdateDcParameters, CpState>;

class ControlEventQueue {
public:
    std::optional<ControlEvent> pop();
    void push(ControlEvent);

private:
    std::queue<ControlEvent> queue;
    std::mutex mutex;
};

} // namespace iso15118::d20::ev
