// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <functional>
#include <string>

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/v2g_message_type.hpp>
#include <iso15118/session/feedback.hpp>

namespace iso15118::session::ev {

namespace feedback {

enum class Signal {
    SETUP_FINISHED,
    CHARGE_LOOP_STARTED,
    CHARGE_LOOP_FINISHED,
    DLINK_TERMINATE,
    DLINK_PAUSE,
    DLINK_ERROR,
};

// Re-use the SECC-side present-limits shape for the EVSE limits reported to the EV.
using DcMaximumLimits = session::feedback::DcMaximumLimits;

struct Callbacks {
    std::function<void(Signal)> signal;
    std::function<void(bool)> ev_power_ready;
    std::function<void()> dc_power_on;
    std::function<void(const d20::AcTargetPower&)> ac_evse_target_power;
    std::function<void()> stop_from_charger;
    std::function<void()> pause_from_charger;
    std::function<void()> v2g_session_finished;
    std::function<void(const std::string&)> selected_protocol;
    std::function<void(const std::string&)> evse_id;
    std::function<void(const DcMaximumLimits&)> dc_evse_present_limits;
    std::function<void(const V2gMessageType&)> v2g_message;
};

} // namespace feedback

class Feedback {
public:
    explicit Feedback(feedback::Callbacks);

    void signal(feedback::Signal) const;
    void ev_power_ready(bool) const;
    void dc_power_on() const;
    void ac_evse_target_power(const d20::AcTargetPower&) const;
    void stop_from_charger() const;
    void pause_from_charger() const;
    void v2g_session_finished() const;
    void selected_protocol(const std::string&) const;
    void evse_id(const std::string&) const;
    void dc_evse_present_limits(const feedback::DcMaximumLimits&) const;
    void v2g_message(const V2gMessageType&) const;

private:
    feedback::Callbacks callbacks;
};

} // namespace iso15118::session::ev
