// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/session.hpp>
#include <iso15118/session/feedback.hpp>

namespace iso15118::d20 {

// The SECC's pause request in a charge loop. In scheduled control mode EVSENotification=Pause may only be sent while
// the applied EVPowerProfileEntry is 0 kW ([V2G20-1198]); until then the request is held back and checked again with
// every charge loop response. In dynamic control mode the caller has already brought the power to 0 kW
// ([V2G20-2115]). PAUSE_NOTIFIED is signalled once per request, when the notification first goes out.
class PauseNotification {
public:
    // Whether the response being built carries EVSENotification=Pause. A stop request takes the pause's place in
    // the response without withdrawing it: a pause the EV was already told about stays allowed ([V2G20-1195]).
    bool update(bool pause_requested, bool stop_requested, Session& session, const session::Feedback& feedback);

private:
    bool m_held_back_logged{false};
};

} // namespace iso15118::d20
