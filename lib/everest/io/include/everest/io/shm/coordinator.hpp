// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/shm/ring_buffer.hpp>
#include <everest/io/shm/shm_types.hpp>

namespace everest::lib::io::shm {

/// \brief Manager-side coordinator for one shared-memory topic ringbuffer.
///
/// The coordinator owns the Manager-side eventfds and hands duplicated file
/// descriptors to publishers/subscribers through the control server. It implements
/// the central publish, dispatch, ACK, and release flow for one topic ring.
class coordinator {
public:
    /// \brief Stable per-topic subscriber slot id.
    using subscriber_id = std::size_t;

    /// \brief Whether a newly joined subscriber can consume immediately.
    enum class subscriber_join_state {
        active,
        pending,
    };

    /// \brief Initial cursor assigned to a subscriber during registration.
    struct subscriber_join_cursor {
        std::uint32_t write_idx{0};
        std::uint64_t sequence{0};
    };

    /// \brief Result returned when a subscriber is added to the coordinator.
    struct subscriber_registration {
        subscriber_id id{0};
        subscriber_join_state state{subscriber_join_state::active};
        subscriber_join_cursor cursor;
    };

    /// \brief Result of applying ACK progress to a subscriber.
    struct ack_result {
        std::uint64_t ack_count{0};
        bool released{false};
    };

    using publication_callback = std::function<void(std::string_view payload, bool retain, bool clear_retained)>;
    using subscriber_dispatch_callback = std::function<void(subscriber_id id)>;

    /// \brief Event descriptors owned by the Manager for one topic.
    struct notification_fds {
        std::shared_ptr<event::event_fd_base> publication;
        std::shared_ptr<event::event_fd_base> release;
        std::shared_ptr<event::event_fd_base> ack;
        std::shared_ptr<event::event_fd_base> broadcast;
    };

    struct subscriber_backpressure_snapshot {
        subscriber_id id{0};
        /// Slots dispatched to this subscriber that have not been ACKed/released yet.
        std::uint64_t outstanding_slots{0};
        /// Highest outstanding_slots value observed since construction, subscriber reuse, or reset_counters().
        std::uint64_t max_observed_outstanding_slots{0};
        /// Slots ACKed by this subscriber since construction, subscriber reuse, or reset_counters().
        std::uint64_t acked_slots{0};
        /// Monotonic coordinator-local sequence of the last ACK progress from this subscriber, or 0 before ACK.
        std::uint64_t last_ack_progress_sequence{0};
        bool active{false};
        bool pending_activation{false};
        bool removed{false};
    };

    explicit coordinator(ring_buffer rb);
    coordinator(ring_buffer rb, notification_fds fds);

    coordinator(const coordinator&) = delete;
    coordinator& operator=(const coordinator&) = delete;
    coordinator(coordinator&&) = delete;
    coordinator& operator=(coordinator&&) = delete;

    /// \brief Initialize a ringbuffer in shared memory for first use.
    static void initialize_ring_buffer(ring_buffer rb, std::uint32_t total_slots, std::uint32_t slot_size,
                                       std::uint32_t target_subscribers);

    /// \brief Duplicated FD for a publisher to notify this coordinator.
    event::unique_fd make_publication_fd() const;

    /// \brief Duplicated FD for a publisher to wait for slot release.
    event::unique_fd make_release_fd() const;

    /// \brief Add a subscriber and return its stable ID plus initial join state.
    ///
    /// Throws std::runtime_error if registering the new subscriber would exceed
    /// shm_max_subscribers_per_topic. Callers that accept registrations on behalf of remote
    /// clients (control_server::build_response) MUST catch this and convert it to a clean
    /// handshake rejection (error_code::resource_error); letting the exception escape the
    /// per-request boundary would allow a misbehaving module to take down the Manager by
    /// repeatedly handshaking until the cap is hit.
    subscriber_registration add_subscriber();

    /// \brief Remove a subscriber and release any slots it was still holding.
    /// \returns True if the subscriber was active and was removed, false if it was already inactive.
    bool remove_subscriber(subscriber_id id);

    /// \brief Duplicated FD for direct coordinator tests to receive dispatch wake-up signals.
    ///
    /// Production control handshakes use client-session wake descriptors created by
    /// control::server. This coordinator-owned topic broadcast FD remains for low-level
    /// topic/coordinator tests that bypass the control server.
    event::unique_fd make_broadcast_fd(subscriber_id id) const;

    /// \brief Duplicated FD for the legacy direct-test per-topic broadcast eventfd.
    ///
    /// Equivalent to make_broadcast_fd() but does not require an existing subscriber
    /// registration. Suitable for tests and for callers that want to observe dispatch wake-ups
    /// without joining as a subscriber.
    event::unique_fd make_topic_broadcast_fd() const;

    /// \brief Duplicated FD for a subscriber to ACK consumed slots.
    ///
    /// Returns a duplicate of the single per-topic ACK eventfd. The subscriber id is validated for
    /// liveness; every active subscriber on this topic receives a duplicate of the *same* underlying
    /// eventfd. Subscriber identity reaches the coordinator through the per-subscriber acked-count
    /// progress field in shared memory (see SubscriberAckProgress).
    event::unique_fd make_ack_fd(subscriber_id id) const;

    /// \brief Duplicated FD for the shared per-topic ACK eventfd.
    ///
    /// Equivalent to make_ack_fd() but does not require an existing subscriber registration. Suitable
    /// for tests and for callers that want to observe ACK traffic without joining as a subscriber.
    event::unique_fd make_topic_ack_fd() const;

    /// \brief Handle a publisher notification and dispatch undispatched slots.
    ///
    /// When dispatch_callback is supplied, it is called once for each subscriber that received at
    /// least one SHM dispatched_count increment in this batch. Production server code uses this to
    /// wake each affected persistent client session once after the SHM counters are visible.
    /// \returns Number of slots dispatched.
    std::size_t handle_publication();
    std::size_t handle_publication(publication_callback callback);
    std::size_t handle_publication(publication_callback callback, subscriber_dispatch_callback dispatch_callback);
    std::size_t handle_pending_publication(publication_callback callback = nullptr);
    std::size_t handle_pending_publication(publication_callback callback,
                                           subscriber_dispatch_callback dispatch_callback);

    /// \brief Handle ACK notifications from the given subscriber.
    ///
    /// Reads the subscriber's monotonic acked-count progress from shared memory, applies any unseen
    /// deltas to that subscriber's pending_slots queue, increments per-slot ack_count, and notifies
    /// the release eventfd when a writer-blocking slot reaches its target subscriber count. Does not
    /// drain the shared per-topic ACK eventfd; use handle_topic_ack() to consume the wake-up signal.
    /// \returns True when at least one ACK released a slot for publisher reuse.
    bool handle_ack(subscriber_id id);

    /// \brief Handle ACK notifications and report the observed ack count delta for this subscriber.
    ack_result handle_ack_details(subscriber_id id);

    /// \brief Drain the per-topic ACK eventfd and apply pending acks across all subscribers.
    /// \returns Aggregate ack_result summing acks observed across all subscribers and whether any
    /// slot was released as a result.
    ack_result handle_topic_ack();
    ack_result handle_pending_acks();

    /// \brief Event FD monitored by the Manager for publisher notifications.
    event::event_fd_base* publication_event_fd();

    /// \brief Legacy direct-test broadcast eventfd.
    ///
    /// Production subscribers receive client-owned wake FDs from control::server instead of this
    /// per-topic FD. This accessor remains for tests that instantiate coordinator/topic directly.
    event::event_fd_base* topic_broadcast_event_fd();

    /// \brief Per-topic ACK eventfd monitored by the Manager.
    ///
    /// All subscribers on this topic share this single eventfd (each holding a duplicate). The
    /// coordinator drains it via handle_topic_ack() and uses per-subscriber SHM progress to
    /// determine which subscribers contributed.
    event::event_fd_base* topic_ack_event_fd();

    /// \brief Backwards-compatible alias for topic_ack_event_fd().
    ///
    /// Older tests query the ack event fd via a subscriber id. Since the eventfd is now per-topic,
    /// the subscriber id is only used to validate that the caller refers to a live subscriber; every
    /// valid id returns the same underlying eventfd.
    event::event_fd_base* ack_event_fd(subscriber_id id);

    /// \brief Current join status for a registered subscriber.
    subscriber_join_state subscriber_join_status(subscriber_id id) const;

    transport_counter_snapshot counter_snapshot() const;
    std::vector<subscriber_backpressure_snapshot> subscriber_backpressure_snapshots() const;
    void reset_counters();

private:
    struct subscriber_state {
        std::deque<std::uint32_t> pending_slots;
        std::uint64_t max_observed_pending_slots{0};
        std::uint64_t acked_slots{0};
        std::uint64_t last_ack_progress_sequence{0};
        /// \brief Last value the coordinator observed for this subscriber's SHM ack progress.
        ///
        /// Compared to the subscriber's atomic acked-count on every ACK signal to derive the number
        /// of new acks to apply to pending_slots. Reset to zero when the subscriber slot is reused.
        std::uint64_t last_observed_ack_progress{0};
        std::uint64_t join_sequence{0};
        bool active{true};
        bool pending_activation{false};
        bool removed{false};
    };

    static event::unique_fd duplicate_fd(int fd);

    subscriber_state& subscriber(subscriber_id id);
    const subscriber_state& subscriber(subscriber_id id) const;

    bool all_slots_released() const;
    void retarget_released_slots(std::uint32_t target_subscribers);
    void activate_pending_subscribers();
    bool acknowledge_slot(std::uint32_t slot_idx);
    void release_removed_subscriber_slot(std::uint32_t slot_idx);
    bool release_if_complete(std::uint32_t slot_idx);
    ack_result handle_ack_for_subscriber(subscriber_state& sub, subscriber_id id);

    ring_buffer m_rb;
    std::shared_ptr<event::event_fd_base> m_publication_fd;
    std::shared_ptr<event::event_fd_base> m_release_fd;
    std::shared_ptr<event::event_fd_base> m_topic_ack_fd;
    std::shared_ptr<event::event_fd_base> m_topic_broadcast_fd;
    std::vector<subscriber_state> m_subscribers;
    std::uint32_t m_dispatch_idx{0};
    std::uint32_t m_pending_activation_count{0};
    std::uint64_t m_last_observed_publication_count{0};

    std::atomic<std::uint64_t> m_messages_published{0};
    std::atomic<std::uint64_t> m_messages_dispatched{0};
    std::atomic<std::uint64_t> m_subscriber_acks_observed{0};
    std::atomic<std::uint64_t> m_slots_released{0};
    std::atomic<std::uint64_t> m_failed_dispatch_attempts{0};
    std::atomic<std::uint64_t> m_subscriber_joins{0};
    std::atomic<std::uint64_t> m_subscriber_removals{0};
    std::atomic<std::uint64_t> m_ack_progress_sequence{0};
};

} // namespace everest::lib::io::shm
