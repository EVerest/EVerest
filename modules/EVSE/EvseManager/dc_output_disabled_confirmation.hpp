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
/// The confirmation needs present values that were measured after the DC output was switched off,
/// because they go out in the WeldingDetectionRes after the PowerDeliveryRes. A measurement already in
/// flight when the output is switched off, or one averaged across the switch off, still shows the old
/// voltage, so the confirmation waits for more than one measurement. Each of them has to show a present
/// current below 5 A (t102 to t103, t104): an EV built to IEC 61851-23:2014 may ask to stop without ramping
/// its current down first.
class DcOutputDisabledConfirmation {
public:
    static constexpr int required_measurements = 2;
    static constexpr float max_current_A = 5.0f;

    /// The DC output was switched off just now: measurements received so far are stale.
    void on_output_off() {
        std::scoped_lock lock(m_mutex);
        m_measurements_since_off = 0;
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
        if (m_pending and m_measurements_since_off >= required_measurements) {
            m_pending = false;
            return true;
        }
        return false;
    }

    /// The HLC stack asks to confirm the DC output as disabled; call after the output was switched off.
    /// \returns true if the confirmation can be sent right away, otherwise it is sent by on_measurement().
    bool request() {
        std::scoped_lock lock(m_mutex);
        if (m_measurements_since_off >= required_measurements) {
            m_pending = false;
            return true;
        }
        m_pending = true;
        return false;
    }

private:
    std::mutex m_mutex;
    // Starts saturated: before the first switch off there is nothing to wait for.
    int m_measurements_since_off{required_measurements};
    bool m_pending{false};
};

} // namespace module
