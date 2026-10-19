// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/control_event.hpp>
#include <iso15118/session/feedback.hpp>

namespace iso15118::session {

// The DC welding detection phase as reported to the module: from PowerDelivery(Stop) until SessionStop,
// renegotiation or teardown (IEC 61851-23:2023 Table CC.9 t104..t107). While it runs, the module reports
// the present voltage of the side B measurement it selected for welding detection and marks those values.
class WeldingDetectionPhase {
public:
    void start(const Feedback& out) {
        running = true;
        voltage_received = false;
        out.signal(feedback::Signal::WELDING_DETECTION_STARTED);
    }

    void finish(const Feedback& out) {
        if (not running) {
            return;
        }
        running = false;
        out.signal(feedback::Signal::WELDING_DETECTION_FINISHED);
    }

    void observe(const d20::PresentVoltageCurrent& values) {
        if (running and values.welding_detection) {
            voltage_received = true;
        }
    }

    bool is_running() const {
        return running;
    }

    // A marked value arrived since start(); the first WeldingDetectionRes waits for it.
    bool has_voltage() const {
        return voltage_received;
    }

private:
    bool running{false};
    bool voltage_received{false};
};

} // namespace iso15118::session
