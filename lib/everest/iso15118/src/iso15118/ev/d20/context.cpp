// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <stdexcept>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>

namespace iso15118::ev::d20 {

Context::Context(feedback::Callbacks feedback_callbacks, MessageExchange& message_exchange_,
                 message_20::datatypes::Identifier evcc_id_,
                 std::vector<message_20::SupportedAppProtocol> advertised_app_protocols_,
                 const std::optional<ControlEvent>& current_control_event_,
                 everest::lib::util::monitor<DcChargeParams>& dc_params_,
                 everest::lib::util::monitor<AcChargeParams>& ac_params_,
                 message_20::datatypes::ServiceCategory requested_service_, SessionOptions options_) :
    feedback(std::move(feedback_callbacks)),
    message_exchange(message_exchange_),
    evcc_id(std::move(evcc_id_)),
    current_control_event(current_control_event_),
    dc_params(dc_params_),
    ac_params(ac_params_),
    selected_service_(requested_service_),
    advertised_app_protocols(std::move(advertised_app_protocols_)),
    session_options(std::move(options_)) {
    secc_clock_.resume(session_options.secc_clock);
}

std::unique_ptr<message_20::Variant> Context::pull_response() {
    return message_exchange.pull_response();
}

message_20::Type Context::peek_response_type() const {
    return message_exchange.peek_response_type();
}

} // namespace iso15118::ev::d20
