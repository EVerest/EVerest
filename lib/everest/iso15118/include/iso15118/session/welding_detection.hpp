// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/d20/control_event.hpp>
#include <iso15118/session/feedback.hpp>

namespace iso15118::session {

// The DC welding detection phase as reported to the module: from PowerDelivery(Stop) until SessionStop,
// renegotiation or teardown (IEC 61851-23:2023 Table CC.9 t104..t107). While it runs, the module reports
// the present voltage of the side B measurement it selected for welding detection and marks those values.
//
// Every WeldingDetectionRes carries a marked value measured after its request (CC.6.3, Table CC.9 t105 to
// t106: the EV may switch its disconnection device between two requests). The first marked value received
// after the request can still be a measurement taken just before it, since a polled source delivers a
// sample up to one poll interval after taking it; the second one was taken after the request.
class WeldingDetectionPhase {
public:
    static constexpr uint8_t UPDATES_PER_RESPONSE = 2;

    void start(const Feedback& out) {
        running = true;
        updates = 0;
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
        if (running and values.welding_detection and updates < UPDATES_PER_RESPONSE) {
            ++updates;
        }
    }

    // A WeldingDetectionReq arrived: its response waits for marked values received from now on.
    void request_received() {
        updates = 0;
    }

    bool is_running() const {
        return running;
    }

    bool has_fresh_voltage() const {
        return updates >= UPDATES_PER_RESPONSE;
    }

    uint8_t updates_since_request() const {
        return updates;
    }

private:
    bool running{false};
    uint8_t updates{0};
};

} // namespace iso15118::session
