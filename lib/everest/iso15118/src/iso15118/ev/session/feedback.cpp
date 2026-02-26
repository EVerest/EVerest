// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/session/feedback.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/evse_session_info.hpp>
namespace iso15118::ev::d20::session {

Feedback::Feedback(feedback::Callbacks callbacks_) : callbacks(std::move(callbacks_)) {
}

void Feedback::evse_session_info(const d20::EVSESessionInfo& info) const {
    call_if_available(callbacks.evse_session_info, info);
}

} // namespace iso15118::ev::d20::session
