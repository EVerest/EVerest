// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <algorithm>
#include <cmath>
#include <mutex>

namespace module {

/// Decides when the HLC stack may report the DC output as disabled after a dc_open_contactor request
/// [IEC 61851-23:2023 Table CC.9 t103, t104].
///
/// t103 has the EVSE disable side B and open its disconnection device before the PowerDeliveryRes, so the
/// confirmation needs the contactor reported open. It also needs present values that were measured after
/// both the switch off and the contactor opening, because they go out in the WeldingDetectionRes after the
/// PowerDeliveryRes. A measurement already in flight at either event, or one averaged across it, still
/// shows the old voltage, so the confirmation waits for more than one measurement. Each of them has to show
/// a present current below 5 A (t102 to t103, t104): an EV built to IEC 61851-23:2014 may ask to stop without
/// ramping its current down first.
class DcOutputDisabledConfirmation {
public:
    static constexpr int required_measurements = 2;
    static constexpr float max_current_A = 5.0f;

    /// The DC output was switched off just now: measurements received so far are stale.
    void on_output_off() {
        std::scoped_lock lock(m_mutex);
        m_measurements_since_off = 0;
    }

    /// The board support reported the contactor open just now: measurements received so far are stale.
    void on_contactor_opened() {
        std::scoped_lock lock(m_mutex);
        m_contactor_open = true;
        m_measurements_since_off = 0;
    }

    void on_contactor_closed() {
        std::scoped_lock lock(m_mutex);
        m_contactor_open = false;
    }

    /// A present value measurement was forwarded to the HLC stack. A current of 5 A or more restarts the count.
    /// \returns true if a pending request is confirmed now, the caller sends the confirmation.
    bool on_measurement(float current_A) {
        std::scoped_lock lock(m_mutex);
        if (std::fabs(current_A) >= max_current_A) {
            m_measurements_since_off = 0;
            return false;
        }
        m_measurements_since_off = std::min(m_measurements_since_off + 1, required_measurements);
        if (m_pending and confirmable()) {
            m_pending = false;
            return true;
        }
        return false;
    }

    /// The HLC stack asks to confirm the DC output as disabled; call after the output was switched off.
    /// \returns true if the confirmation can be sent right away, otherwise it is sent by on_measurement().
    bool request() {
        std::scoped_lock lock(m_mutex);
        if (confirmable()) {
            m_pending = false;
            return true;
        }
        m_pending = true;
        return false;
    }

private:
    bool confirmable() const {
        return m_contactor_open and m_measurements_since_off >= required_measurements;
    }

    std::mutex m_mutex;
    // Starts saturated and open: before the first power on there is nothing to wait for.
    int m_measurements_since_off{required_measurements};
    bool m_contactor_open{true};
    bool m_pending{false};
};

} // namespace module
