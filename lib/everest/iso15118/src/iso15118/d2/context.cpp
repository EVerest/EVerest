// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/context.hpp>

#include <cstring>
#include <stdexcept>
#include <utility>

#include <iso15118/detail/helper.hpp>

namespace iso15118::d2 {

Context::Context(session::feedback::Callbacks callbacks, d2::SessionConfig config,
                 std::optional<PauseContext>& pause_ctx_, const std::optional<d20::ControlEvent>& control_event,
                 MessageExchange& message_exchange_, d20::Timeouts& timeouts_) :
    feedback(std::move(callbacks)),
    session_config(std::move(config)),
    pause_ctx(pause_ctx_),
    current_control_event(control_event),
    message_exchange(message_exchange_),
    timeouts(timeouts_) {
}

std::unique_ptr<message_2::Variant> Context::pull_request() {
    return message_exchange.pull_request();
}

message_2::Type Context::peek_request_type() const {
    return message_exchange.peek_request_type();
}

void Context::report_ev_status(const dt::DC_EVStatus& status) {
    const auto unchanged = last_reported_ev_status.has_value() and
                           last_reported_ev_status->ev_ready == status.ev_ready and
                           last_reported_ev_status->ev_error_code == status.ev_error_code and
                           last_reported_ev_status->ev_ress_soc == status.ev_ress_soc;
    if (unchanged) {
        return;
    }
    last_reported_ev_status = status;

    session::feedback::DcEvStatus ev_status;
    ev_status.ready = status.ev_ready;
    ev_status.error_code = status.ev_error_code;
    ev_status.ress_soc = status.ev_ress_soc;
    // cabin_conditioning / ress_conditioning are DIN SPEC 70121 only; ISO 15118-2 does not carry them.
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

void Context::setup_header(message_2::Header& header) const {
    header.session_id = session_id;
}

} // namespace iso15118::d2

namespace iso15118::d2 {

MessageExchange::MessageExchange(io::StreamOutputView output_) : response(std::move(output_)) {
}

void MessageExchange::set_request(std::unique_ptr<message_2::Variant> new_request) {
    if (request) {
        throw std::runtime_error("Previous V2G message has not been handled yet");
    }
    request = std::move(new_request);
}

std::unique_ptr<message_2::Variant> MessageExchange::pull_request() {
    if (not request) {
        throw std::runtime_error("Tried to access V2G message, but there is none");
    }
    return std::move(request);
}

void MessageExchange::set_raw_response(const uint8_t* data, size_t len, message_2::Type type) {
    if (data == nullptr or len == 0) {
        logf_error("Refusing to stage an empty raw relay response");
        return;
    }
    if (len > response.payload_len) {
        logf_error("Raw relay response (%zu bytes) exceeds the output buffer (%zu bytes)", len, response.payload_len);
        return;
    }
    std::memcpy(response.payload, data, len);
    response_size = len;
    response_available = true;
    payload_type = io::v2gtp::PayloadType::SAP;
    response_type = type;
}

std::tuple<bool, size_t, io::v2gtp::PayloadType, message_2::Type> MessageExchange::check_and_clear_response() {
    auto retval = std::make_tuple(response_available, response_size, payload_type, response_type);

    response_available = false;
    response_size = 0;
    response_type = message_2::Type::None;

    return retval;
}

message_2::Type MessageExchange::peek_request_type() const {
    if (not request) {
        logf_warning("Tried to access V2G message, but there is none");
        return message_2::Type::None;
    }
    return request->get_type();
}

} // namespace iso15118::d2
