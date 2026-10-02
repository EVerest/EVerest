// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/context.hpp>

namespace iso15118::ev::din {

Context::Context(feedback::Callbacks feedback_callbacks, MessageExchange& message_exchange_, EvSessionParams params__,
                 const std::optional<ControlEvent>& current_control_event_,
                 everest::lib::util::monitor<DcChargeParams>& dc_params_, bool has_cp_state_feedback__,
                 std::optional<message_din::datatypes::SessionId> resumed_session_id__) :
    feedback(std::move(feedback_callbacks)),
    message_exchange(message_exchange_),
    params_(std::move(params__)),
    current_control_event(current_control_event_),
    dc_params(dc_params_),
    has_cp_state_feedback_(has_cp_state_feedback__),
    resumed_session_id_(resumed_session_id__) {
    if (resumed_session_id_.has_value()) {
        session_id = resumed_session_id_.value();
    }
}

} // namespace iso15118::ev::din
