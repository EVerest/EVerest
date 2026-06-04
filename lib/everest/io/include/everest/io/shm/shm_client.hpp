// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/fd_event_sync_interface.hpp>
#include <everest/io/shm/control_client.hpp>
#include <everest/io/shm/shm_types.hpp>
#include <everest/io/shm/topic.hpp>

namespace everest::lib::io::shm {

/// \brief Publisher behaviour when all ring slots are still held by subscribers.
enum class publish_full_buffer_behavior {
    block,
    fail,
};

/// \brief Per-publication options.
struct publish_options {
    publish_full_buffer_behavior full_buffer_behavior{publish_full_buffer_behavior::block};
    bool retain{false};
    bool clear_retained{false};
};

/// \brief Per-subscription options.
struct subscribe_options {
    /// Require subscriber construction from the manager-provided initial join cursor.
    bool use_join_cursor{true};
};

/// \brief Construction options for shm_client.
struct client_options {
    std::string client_id;
    control::client_options control;
    /// Optional timing sink for single-threaded benchmark/profiling tools. Normal users should leave this unset.
    profile_callback profile;
    /// Optional count sink for benchmark/profiling tools. Values are counts, not nanosecond durations.
    profile_metric_callback profile_metric;
};

/// \brief Client-side SHM IO API.
///
/// This class owns control handshakes, client-side shared memory mappings,
/// publisher/subscriber shm::topic objects, subscription callbacks, and event registrations.
///
/// Lifetime rules:
/// - The client owns all shared-memory mappings it opens during connect(), publish setup, or subscribe().
/// - The client owns the event FDs received from control handshakes; users must not close them.
/// - Message callback payload and topic views are valid only for the duration of the callback.
/// - unsubscribe(topic) removes the subscriber topic object and invalidates any internal topic handle.
/// - disconnect() removes all publisher/subscriber topic objects, unmaps SHM, closes owned FDs, and
///   invalidates event-loop registrations.
/// - After disconnect(), publish(), subscribe(), and unsubscribe() fail with io_status::not_open until
///   connect() succeeds again.
class shm_client : public event::fd_event_register_interface, public event::fd_event_sync_interface {
public:
    using message_callback = std::function<void(std::string_view topic, std::string_view payload)>;
    using sequence_error_callback = topic::on_sequence_error_callback;
    using error_callback = std::function<void(io_status status, std::string_view message)>;

    explicit shm_client(client_options options);
    ~shm_client() override;

    shm_client(const shm_client&) = delete;
    shm_client& operator=(const shm_client&) = delete;
    shm_client(shm_client&&) noexcept;
    shm_client& operator=(shm_client&&) noexcept;

    const client_options& options() const;
    const std::string& client_id() const;

    io_result connect();
    io_result disconnect();
    bool is_connected() const;

    void set_error_handler(error_callback handler);

    io_result publish(std::string_view topic, std::string_view payload, publish_options options = {});
    io_result subscribe(std::string_view topic, message_callback callback, subscribe_options options = {});
    io_result subscribe(std::string_view topic, message_callback callback, sequence_error_callback sequence_error,
                        subscribe_options options = {});
    io_result subscribe(const std::vector<std::string>& topics, message_callback callback,
                        subscribe_options options = {});
    io_result unsubscribe(std::string_view topic);

    struct registered_topics_result {
        io_status status{io_status::ok};
        std::string message;
        std::vector<std::string> topics;

        explicit operator bool() const;
    };

    /// \brief Query the Manager-owned exact SHM topic registry through the control socket.
    ///
    /// Observer clients (e.g. shm_to_mqtt_bridge) use this to discover which framework-local
    /// SHM topics currently exist without parsing the shared-memory segment directly. The
    /// returned list contains exact topic names; wildcard expansion is the caller's job.
    registered_topics_result registered_topics();

    transport_counter_snapshot counter_snapshot() const;
    void reset_counters();

    /// Register all client-owned topic/control event FDs with a caller-owned event loop.
    bool register_events(event::fd_event_handler& handler) override;
    bool unregister_events(event::fd_event_handler& handler) override;

    /// Return the internal event-loop FD for users that prefer fd_event_sync_interface style integration.
    int get_poll_fd() override;
    event::sync_status sync() override;

private:
    struct impl;
    std::unique_ptr<impl> m_impl;
};

} // namespace everest::lib::io::shm
