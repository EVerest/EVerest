// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <stdexcept>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>

namespace iso15118::ev::d20 {

Context::Context(feedback::Callbacks feedback_callbacks, MessageExchange& message_exchange_,
                 message_20::datatypes::Identifier evcc_id_,
                 std::vector<message_20::SupportedAppProtocol> advertised_app_protocols_, session::Latches& latches_,
                 everest::lib::util::monitor<params::DcChargeParams>& dc_params_,
                 everest::lib::util::monitor<params::AcChargeParams>& ac_params_,
                 message_20::datatypes::ServiceCategory requested_service_, SessionOptions options_) :
    welding_detection(options_.welding_detection),
    feedback(std::move(feedback_callbacks)),
    message_exchange(message_exchange_),
    evcc_id(std::move(evcc_id_)),
    latches(latches_),
    dc_params(dc_params_),
    ac_params(ac_params_),
    selected_service_(requested_service_),
    advertised_app_protocols(std::move(advertised_app_protocols_)),
    session_options(std::move(options_)) {
    if (session_options.resume.has_value()) {
        secc_clock_.resume(session_options.resume->secc_clock);
    }
}

EnergyRequest Context::energy_request() const {
    const auto& request = session_options.energy_request;
    auto departure = request.departure_in(secc_clock_.steady_now());
    // [V2G20-2103]: DepartureTime is above 0, so a passed departure is not stated.
    if (departure == 0U) {
        departure.reset();
    }
    return {departure, request.stated_energy_wh(), get_dc_params().energy_capacity};
}

std::unique_ptr<message_20::ResponseVariant> Context::pull_response() {
    return message_exchange.pull_response();
}

message_20::Type Context::peek_response_type() const {
    return message_exchange.peek_response_type();
}

} // namespace iso15118::ev::d20
