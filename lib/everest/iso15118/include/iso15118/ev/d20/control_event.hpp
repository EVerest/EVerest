// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <variant>

namespace iso15118::ev::d20 {

// EV-side control events, delivered by the Session with Event::CONTROL_MESSAGE.

// Graceful stop: SessionStop(Terminate) after PowerDelivery(Stop) (+ DC_WeldingDetection).
class StopCharging {
public:
    explicit StopCharging(bool stop_) : stop(stop_) {
    }

    operator bool() const {
        return stop;
    }

private:
    bool stop;
};

// Graceful pause: same walk, SessionStop(Pause); the session id may be re-joined later.
class PauseCharging {
public:
    explicit PauseCharging(bool pause_) : pause(pause_) {
    }

    operator bool() const {
        return pause;
    }

private:
    bool pause;
};

// IEC 61851-1 control pilot state applied by the EV: true in state C or D. Gates the first
// DC_CableCheckReq [V2G2-847] when the owner reports the pilot at all.
struct CpState {
    bool c_or_d{false};
};

using ControlEvent = std::variant<StopCharging, PauseCharging, CpState>;

} // namespace iso15118::ev::d20
