// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/pause_notification.hpp>

#include <chrono>
#include <cstdint>

#include <iso15118/detail/helper.hpp>

namespace iso15118::d20 {

bool PauseNotification::update(bool pause_requested, bool stop_requested, Session& session,
                               const session::Feedback& feedback) {
    if (not pause_requested) {
        m_held_back_logged = false;
        session.secc_pause_notified = false;
        return false;
    }
    if (stop_requested) {
        return false;
    }
    const bool scheduled = session.get_selected_services().selected_control_mode == dt::ControlMode::Scheduled;
    const auto now_s = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    if (scheduled and not(session.ev_power_profile.has_value() and session.ev_power_profile->zero_power_at(now_s))) {
        if (not m_held_back_logged) {
            logf_info("Pause requested in scheduled control mode: waiting for a 0 kW EVPowerProfile entry "
                      "[V2G20-1198]");
            m_held_back_logged = true;
        }
        return false;
    }
    if (not session.secc_pause_notified) {
        session.secc_pause_notified = true;
        feedback.signal(session::feedback::Signal::PAUSE_NOTIFIED);
    }
    return true;
}

} // namespace iso15118::d20
