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
#include <everest/io/shm/control_protocol.hpp>
#include <everest/io/shm/shm_types.hpp>

namespace everest::lib::io::shm {

/// \brief Construction options for shm_server.
struct server_options {
    /// POSIX shared memory object name. The server owns creation, mapping, and optional unlinking.
    std::string shm_name;
    /// Total shared memory segment size. If zero, the server uses the minimum size required by topics.
    std::uint64_t segment_size{0};
    /// Fixed-capacity topic registry entry count. If zero, the server uses topics.size().
    std::uint32_t topic_registry_capacity{0};
    /// Default ring slot count for topic definitions with total_slots set to zero.
    std::uint32_t default_ring_slots{0};
    /// Default ring slot payload size for topic definitions with slot_size set to zero.
    std::uint32_t default_slot_size{0};
    /// Unix datagram control socket name used by clients for SHM handshakes.
    std::string control_socket_name;
    bool control_socket_abstract_namespace{true};
    /// Topic registry entries to initialize in the shared memory segment.
    std::vector<topic_definition> topics;
    /// If true, close() unlinks the server-owned shared memory object after unmapping it.
    bool unlink_shm_on_close{true};
    /// If true, close() unlinks the server-owned filesystem socket path when not using the abstract namespace.
    bool unlink_control_socket_on_close{true};
    /// Optional timing sink for single-threaded benchmark/profiling tools. Normal users should leave this unset.
    profile_callback profile;
    /// Optional count sink for benchmark/profiling tools. Values are counts, not nanosecond durations.
    profile_metric_callback profile_metric;
};

/// \brief Point-in-time subscriber state reported by shm_server.
struct subscriber_snapshot {
    std::string topic;
    std::string client_id;
    std::uint32_t subscriber_id{0};
    control::join_state state{control::join_state::active};
};

/// \brief Broker-side SHM IO API.
///
/// This class owns the SHM segment, segment header/topic registry,
/// topic_runtime_registry, control::server, one coordinator per topic, and all event
/// registrations for control, publication, ACK, liveness cleanup, and shutdown.
///
/// Lifetime rules:
/// - The server owns the shared memory mapping and control socket after open() succeeds.
/// - Raw file descriptors stay owned by the server and its low-level SHM primitives.
/// - Event handlers only observe those descriptors; unregister_events() or close() removes them.
/// - subscriber_snapshot values are copies and remain valid after close(), but represent only
///   the point-in-time state returned by subscriber_snapshots().
/// - After close(), event-loop registration is invalidated and all server-side topic state is gone.
class shm_server : public event::fd_event_register_interface, public event::fd_event_sync_interface {
public:
    using error_callback = std::function<void(io_status status, std::string_view message)>;

    explicit shm_server(server_options options);
    ~shm_server() override;

    shm_server(const shm_server&) = delete;
    shm_server& operator=(const shm_server&) = delete;
    shm_server(shm_server&&) noexcept;
    shm_server& operator=(shm_server&&) noexcept;

    const server_options& options() const;

    io_result open();
    io_result close();
    bool is_open() const;

    void set_error_handler(error_callback handler);

    std::vector<subscriber_snapshot> subscriber_snapshots() const;
    std::vector<subscriber_snapshot> subscriber_snapshots(std::string_view topic) const;

    transport_counter_snapshot counter_snapshot() const;
    void reset_counters();

    /// Register all server-owned event FDs with a caller-owned event loop.
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
