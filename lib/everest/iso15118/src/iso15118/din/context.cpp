// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/context.hpp>

#include <stdexcept>
#include <utility>

#include <iso15118/detail/helper.hpp>

namespace iso15118::din {

MessageExchange::MessageExchange(io::StreamOutputView output_) : response(std::move(output_)) {
}

void MessageExchange::set_request(std::unique_ptr<message_din::Variant> new_request) {
    if (request) {
        throw std::runtime_error("Previous V2G message has not been handled yet");
    }
    request = std::move(new_request);
}

std::unique_ptr<message_din::Variant> MessageExchange::pull_request() {
    if (not request) {
        throw std::runtime_error("Tried to access V2G message, but there is none");
    }
    return std::move(request);
}

std::tuple<bool, size_t, io::v2gtp::PayloadType, message_din::Type> MessageExchange::check_and_clear_response() {
    auto retval = std::make_tuple(response_available, response_size, payload_type, response_type);

    response_available = false;
    response_size = 0;
    response_type = message_din::Type::None;

    return retval;
}

message_din::Type MessageExchange::peek_request_type() const {
    if (not request) {
        logf_warning("Tried to access V2G message, but there is none");
        return message_din::Type::None;
    }
    return request->get_type();
}

Context::Context(session::feedback::Callbacks feedback_callbacks, SessionConfig session_config_,
                 const std::optional<d20::ControlEvent>& current_control_event_, MessageExchange& message_exchange_,
                 d20::Timeouts& timeouts_) :
    feedback(std::move(feedback_callbacks)),
    session_config(std::move(session_config_)),
    current_control_event{current_control_event_},
    message_exchange(message_exchange_),
    timeouts(timeouts_) {
}

void Context::report_ev_status(const message_din::datatypes::DcEvStatus& status) {
    const auto unchanged = last_reported_ev_status.has_value() and
                           last_reported_ev_status->ev_ready == status.ev_ready and
                           last_reported_ev_status->ev_error_code == status.ev_error_code and
                           last_reported_ev_status->ev_ress_soc == status.ev_ress_soc and
                           last_reported_ev_status->ev_cabin_conditioning == status.ev_cabin_conditioning and
                           last_reported_ev_status->ev_ress_conditioning == status.ev_ress_conditioning;
    if (unchanged) {
        return;
    }
    last_reported_ev_status = status;

    session::feedback::DcEvStatus ev_status;
    ev_status.ready = status.ev_ready;
    ev_status.error_code = status.ev_error_code;
    ev_status.ress_soc = status.ev_ress_soc;
    ev_status.cabin_conditioning = status.ev_cabin_conditioning;
    ev_status.ress_conditioning = status.ev_ress_conditioning;
    feedback.dc_ev_status(ev_status);
}

void Context::report_charge_progress(const session::feedback::DcEvChargeProgress& progress) {
    const auto unchanged =
        last_reported_charge_progress.has_value() and
        last_reported_charge_progress->remaining_time_to_full_soc == progress.remaining_time_to_full_soc and
        last_reported_charge_progress->remaining_time_to_bulk_soc == progress.remaining_time_to_bulk_soc and
        last_reported_charge_progress->charging_complete == progress.charging_complete and
        last_reported_charge_progress->bulk_charging_complete == progress.bulk_charging_complete;
    if (unchanged) {
        return;
    }
    last_reported_charge_progress = progress;
    feedback.dc_ev_charge_progress(progress);
}

std::unique_ptr<message_din::Variant> Context::pull_request() {
    return message_exchange.pull_request();
}

message_din::Type Context::peek_request_type() const {
    return message_exchange.peek_request_type();
}

void Context::setup_header(message_din::Header& header) const {
    header.session_id = session_id;
}

} // namespace iso15118::din
