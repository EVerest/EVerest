// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <cstdint>

#include <iso15118/d20/session.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/session/feedback.hpp>

namespace iso15118::d20 {

// The SECC's pause request in a charge loop. In scheduled control mode EVSENotification=Pause may only be sent while
// the applied EVPowerProfileEntry is 0 kW ([V2G20-1198]); until then the request is held back and checked again with
// every charge loop response. In dynamic control mode the caller has already brought the power to 0 kW
// ([V2G20-2115]). PAUSE_NOTIFIED is signalled once per request, when the notification first goes out.
class PauseNotification {
public:
    // Whether the response being built carries EVSENotification=Pause.
    bool update(bool pause_requested, const Session& session, const session::Feedback& feedback) {
        if (not pause_requested) {
            m_notified = false;
            m_held_back_logged = false;
            return false;
        }
        const bool scheduled = session.get_selected_services().selected_control_mode == dt::ControlMode::Scheduled;
        const auto now_s = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
        if (scheduled and
            not(session.ev_power_profile.has_value() and session.ev_power_profile->zero_power_at(now_s))) {
            if (not m_held_back_logged) {
                logf_info("Pause requested in scheduled control mode: waiting for a 0 kW EVPowerProfile entry "
                          "[V2G20-1198]");
                m_held_back_logged = true;
            }
            return false;
        }
        if (not m_notified) {
            m_notified = true;
            feedback.signal(session::feedback::Signal::PAUSE_NOTIFIED);
        }
        return true;
    }

private:
    bool m_notified{false};
    bool m_held_back_logged{false};
};

} // namespace iso15118::d20
