// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/session/stop_reason.hpp>

namespace iso15118::ev::session {

StopReason combine(std::optional<StopReason> ev, std::optional<StopReason> evse) {
    if (ev.has_value() and evse.has_value() and is_pause(*evse) and not is_pause(*ev)) {
        return *ev;
    }
    if (evse.has_value()) {
        return *evse;
    }
    return ev.value_or(StopReason::EvStop);
}

} // namespace iso15118::ev::session
