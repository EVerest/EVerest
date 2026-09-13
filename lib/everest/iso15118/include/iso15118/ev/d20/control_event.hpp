// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <variant>

namespace iso15118::ev::d20 {

// EV-side control event. Minimal for now: a single StopCharging directive.
// Per-state CONTROL_MESSAGE handlers / full control parity are deferred; this
// lays only the delivery seam.
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

using ControlEvent = std::variant<StopCharging>;

} // namespace iso15118::ev::d20
