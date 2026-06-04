// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace everest::lib::io::shm {

/// \brief Topic ring configuration used when creating an SHM server.
struct topic_definition {
    /// Exact topic name stored in the fixed segment registry.
    std::string name;
    /// Number of slots for this topic, or zero to use the server default.
    std::uint32_t total_slots{0};
    /// Payload capacity of each slot, or zero to use the server default.
    std::uint32_t slot_size{0};
};

/// \brief Point-in-time low-level SHM transport counters.
///
/// Components own and increment counters for the events they directly observe:
/// - topic publishers count publish success, local publish failures, full-buffer attempts, drops, and slot reuse.
/// - topic subscribers count callback dispatches and ACK writes.
/// - coordinators count publication notifications, subscriber dispatch signals, observed ACKs, slot releases, and
///   subscriber joins/removals.
/// - server/client wrappers aggregate their owned reusable components and only add wrapper-local liveness/API signals.
///
/// counter_snapshot() returns monotonic counters since construction or the last reset_counters(). reset_counters()
/// clears only the owning object's counters; it does not alter transport state, ringbuffer contents, subscriptions, or
/// eventfds.
struct transport_counter_snapshot {
    std::uint64_t messages_published{0};
    std::uint64_t messages_dispatched{0};
    std::uint64_t subscriber_acks_observed{0};
    std::uint64_t slots_released{0};
    std::uint64_t slots_reused{0};
    std::uint64_t blocked_publish_attempts{0};
    std::uint64_t dropped_publish_attempts{0};
    std::uint64_t failed_publish_attempts{0};
    std::uint64_t failed_dispatch_attempts{0};
    std::uint64_t subscriber_joins{0};
    std::uint64_t subscriber_removals{0};
    std::uint64_t liveness_disconnects{0};
};

/// \brief Add all counter values from source into target.
void add_transport_counters(transport_counter_snapshot& target, const transport_counter_snapshot& source);

/// \brief Timing stages reported through profile_callback.
enum class profile_stage {
    publish_call,
    server_dispatch,
    subscriber_callback_path,
    ack_release,
};

/// \brief Callback for measured SHM transport durations.
using profile_callback = std::function<void(profile_stage stage, std::uint64_t duration_ns)>;

/// \brief Count metrics reported through profile_metric_callback.
enum class profile_metric {
    publication_batch_depth,
    subscriber_dispatch_batch_depth,
    ack_batch_depth,
    event_loop_ready_fd_count,
};

/// \brief Callback for measured SHM transport counts.
using profile_metric_callback = std::function<void(profile_metric metric, std::uint64_t value)>;

std::string_view to_string(profile_stage stage);

std::string_view to_string(profile_metric metric);

/// \brief Status code returned by the reusable SHM IO API.
enum class io_status {
    ok,
    not_open,
    already_open,
    unknown_topic,
    invalid_options,
    protocol_error,
    resource_error,
    not_implemented,
};

std::string_view to_string(io_status status);

/// \brief Result object returned by reusable SHM IO API methods.
struct io_result {
    io_status status{io_status::ok};
    std::string message;

    explicit operator bool() const;
};

} // namespace everest::lib::io::shm
