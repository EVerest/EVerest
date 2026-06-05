// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/io/shm/shm_types.hpp>

namespace everest::lib::io::shm {

void add_transport_counters(transport_counter_snapshot& target, const transport_counter_snapshot& source) {
    target.messages_published += source.messages_published;
    target.messages_dispatched += source.messages_dispatched;
    target.subscriber_acks_observed += source.subscriber_acks_observed;
    target.slots_released += source.slots_released;
    target.slots_reused += source.slots_reused;
    target.blocked_publish_attempts += source.blocked_publish_attempts;
    target.dropped_publish_attempts += source.dropped_publish_attempts;
    target.failed_publish_attempts += source.failed_publish_attempts;
    target.failed_dispatch_attempts += source.failed_dispatch_attempts;
    target.subscriber_joins += source.subscriber_joins;
    target.subscriber_removals += source.subscriber_removals;
    target.liveness_disconnects += source.liveness_disconnects;
}

std::string_view to_string(profile_stage stage) {
    switch (stage) {
    case profile_stage::publish_call:
        return "publish_call";
    case profile_stage::server_dispatch:
        return "server_dispatch";
    case profile_stage::subscriber_callback_path:
        return "subscriber_callback_path";
    case profile_stage::ack_release:
        return "ack_release";
    }
    return "unknown";
}

std::string_view to_string(profile_metric metric) {
    switch (metric) {
    case profile_metric::publication_batch_depth:
        return "publication_batch_depth";
    case profile_metric::subscriber_dispatch_batch_depth:
        return "subscriber_dispatch_batch_depth";
    case profile_metric::ack_batch_depth:
        return "ack_batch_depth";
    case profile_metric::event_loop_ready_fd_count:
        return "event_loop_ready_fd_count";
    }
    return "unknown";
}

std::string_view to_string(io_status status) {
    switch (status) {
    case io_status::ok:
        return "ok";
    case io_status::not_open:
        return "not_open";
    case io_status::already_open:
        return "already_open";
    case io_status::unknown_topic:
        return "unknown_topic";
    case io_status::invalid_options:
        return "invalid_options";
    case io_status::protocol_error:
        return "protocol_error";
    case io_status::resource_error:
        return "resource_error";
    case io_status::not_implemented:
        return "not_implemented";
    }
    return "unknown";
}

io_result::operator bool() const {
    return status == io_status::ok;
}

} // namespace everest::lib::io::shm
