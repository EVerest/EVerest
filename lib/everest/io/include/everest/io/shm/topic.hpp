// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/shm/ring_buffer.hpp>
#include <everest/io/shm/shm_types.hpp>

namespace everest::lib::io::shm {

/// \brief Publisher or subscriber endpoint for one shared-memory topic ring.
///
/// A topic object owns the file descriptors and ring cursor state needed by one
/// publisher or subscriber. Publishers write payloads into the ring and notify the
/// Manager; subscribers consume coordinator-dispatched slots, invoke the user callback,
/// and acknowledge consumed slots.
class topic : public event::fd_event_register_interface {
public:
    /// \brief Callback receiving a copied payload string.
    using on_data_callback = std::function<void(const std::string& data)>;
    /// Callback receiving a view directly into the shared-memory slot payload.
    /// The view is valid only for the duration of the callback.
    using on_data_view_callback = std::function<void(std::string_view data)>;

    /// \brief Result of validating the sequence number on a consumed slot.
    enum class sequence_status {
        accepted,
        stale,
        gap,
    };

    /// \brief Details passed to sequence error callbacks.
    struct sequence_validation_result {
        sequence_status status{sequence_status::accepted};
        std::uint64_t expected_sequence{0};
        std::uint64_t actual_sequence{0};
        std::uint32_t slot_idx{0};
    };

    using on_sequence_error_callback = std::function<void(const sequence_validation_result& result)>;

    /// \brief Initial read cursor assigned by the Manager during subscription.
    struct subscriber_cursor {
        std::uint32_t write_idx{0};
        std::uint64_t sequence{0};
    };

    /// \brief Behaviour when a publisher encounters a full ring.
    enum class full_buffer_policy {
        block_publisher,
        fail_immediately,
    };

    /// \brief Construct a topic as a Publisher.
    /// \param rb The shared memory ringbuffer.
    /// \param pub_fd FD to notify the Manager when data is written.
    /// \param release_fd FD to wait on when the buffer is full.
    static std::unique_ptr<topic> make_publisher(ring_buffer rb, event::unique_fd&& pub_fd,
                                                 event::unique_fd&& release_fd);
    static std::unique_ptr<topic> make_publisher(ring_buffer rb, std::shared_ptr<event::event_fd_base> pub_fd,
                                                 std::shared_ptr<event::event_fd_base> release_fd);

    /// \brief Construct a topic as a Subscriber.
    /// \param rb The shared memory ringbuffer.
    /// \param broadcast_fd FD to wait on for new data signals.
    /// \param ack_fd FD to notify the Manager when data has been read.
    /// \param callback Function to call when new data arrives.
    /// \param subscriber_id Coordinator-assigned subscriber id used to index this subscriber's
    ///        progress atomics in shared memory. Must be supplied explicitly — silently defaulting
    ///        to 0 would let multiple subscribers collide on the same progress-table entry and
    ///        corrupt cross-process dispatch/ack accounting. Single-subscriber tests should pass 0
    ///        explicitly to document the assumption.
    static std::unique_ptr<topic> make_subscriber(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                                  event::unique_fd&& ack_fd, on_data_callback callback,
                                                  std::uint32_t subscriber_id);

    /// \brief Construct a topic as a Subscriber with sequence validation error reporting.
    /// \copydetails make_subscriber(ring_buffer, event::unique_fd&&, event::unique_fd&&, on_data_callback,
    /// std::uint32_t) \param sequence_error_callback Function to call when a stale or skipped sequence is detected.
    static std::unique_ptr<topic> make_subscriber(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                                  event::unique_fd&& ack_fd, on_data_callback callback,
                                                  on_sequence_error_callback sequence_error_callback,
                                                  std::uint32_t subscriber_id);

    /// \brief Construct a topic as a Subscriber from a manager-provided join cursor.
    /// \copydetails make_subscriber(ring_buffer, event::unique_fd&&, event::unique_fd&&, on_data_callback,
    /// std::uint32_t) \param cursor Ring position and next expected sequence captured during the subscriber join
    /// handshake.
    static std::unique_ptr<topic> make_subscriber(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                                  event::unique_fd&& ack_fd, on_data_callback callback,
                                                  subscriber_cursor cursor, std::uint32_t subscriber_id);

    /// \brief Construct a topic as a Subscriber from a manager-provided join cursor.
    /// \copydetails make_subscriber(ring_buffer, event::unique_fd&&, event::unique_fd&&, on_data_callback,
    /// std::uint32_t) \param cursor Ring position and next expected sequence captured during the subscriber join
    /// handshake. \param sequence_error_callback Function to call when a stale or skipped sequence is detected.
    static std::unique_ptr<topic> make_subscriber(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                                  event::unique_fd&& ack_fd, on_data_callback callback,
                                                  subscriber_cursor cursor,
                                                  on_sequence_error_callback sequence_error_callback,
                                                  std::uint32_t subscriber_id);

    /// \brief Construct a Subscriber whose callback receives a callback-duration payload view.
    static std::unique_ptr<topic> make_subscriber_view(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                                       event::unique_fd&& ack_fd, on_data_view_callback callback,
                                                       std::uint32_t subscriber_id);

    /// \brief Construct a Subscriber whose callback receives a callback-duration payload view.
    static std::unique_ptr<topic> make_subscriber_view(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                                       event::unique_fd&& ack_fd, on_data_view_callback callback,
                                                       on_sequence_error_callback sequence_error_callback,
                                                       std::uint32_t subscriber_id);

    /// \brief Construct a Subscriber from a manager-provided cursor with a callback-duration payload view.
    static std::unique_ptr<topic> make_subscriber_view(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                                       event::unique_fd&& ack_fd, on_data_view_callback callback,
                                                       subscriber_cursor cursor, std::uint32_t subscriber_id);

    /// \brief Construct a Subscriber from a manager-provided cursor with a callback-duration payload view.
    static std::unique_ptr<topic> make_subscriber_view(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                                       event::unique_fd&& ack_fd, on_data_view_callback callback,
                                                       subscriber_cursor cursor,
                                                       on_sequence_error_callback sequence_error_callback,
                                                       std::uint32_t subscriber_id);
    static std::unique_ptr<topic> make_subscriber_view(ring_buffer rb,
                                                       std::shared_ptr<event::event_fd_base> broadcast_fd,
                                                       std::shared_ptr<event::event_fd_base> ack_fd,
                                                       on_data_view_callback callback, subscriber_cursor cursor,
                                                       std::uint32_t subscriber_id);
    static std::unique_ptr<topic> make_subscriber_view(ring_buffer rb,
                                                       std::shared_ptr<event::event_fd_base> broadcast_fd,
                                                       std::shared_ptr<event::event_fd_base> ack_fd,
                                                       on_data_view_callback callback, subscriber_cursor cursor,
                                                       on_sequence_error_callback sequence_error_callback,
                                                       std::uint32_t subscriber_id);

    /// \brief Publishes data to the ringbuffer.
    /// \param data The JSON string payload.
    /// \param block If true, block until a slot is free if the buffer is full.
    /// \returns True if data was published or dropped because there are no subscribers, false if the buffer was full
    ///          and block was false, or if the payload exceeds the slot size.
    bool publish(const std::string& data, bool block = true);
    bool publish(const char* data, bool block = true);
    bool publish(std::string_view data, bool block = true);

    /// \brief Publishes data to the ringbuffer using an explicit full-buffer policy.
    /// \param data The JSON string payload.
    /// \param policy Controls whether a full ringbuffer blocks the publisher or fails immediately.
    /// \returns True if data was published or dropped because there are no subscribers, false if the buffer was full
    ///          and policy is fail_immediately, or if the payload exceeds the slot size.
    bool publish(const std::string& data, full_buffer_policy policy);
    bool publish(const char* data, full_buffer_policy policy);
    bool publish(std::string_view data, full_buffer_policy policy);
    bool publish(std::string_view data, full_buffer_policy policy, bool retain, bool clear_retained);

    /// \returns The event FD that should be monitored for activity.
    /// For Subscribers, this is the broadcast_fd. For Publishers, this is the release_fd.
    event::event_fd_base* get_event_fd();

    /// \brief Processes incoming activity on the event FD.
    /// For Subscribers, this reads available data and calls the callback.
    /// For Publishers, this handles the release signal (unblocks the next publish).
    void handle_event();

    /// \brief Processes incoming activity and returns the drained eventfd counter.
    /// For subscribers this is the number of dispatched slots consumed. For publishers this is the release counter.
    std::uint64_t handle_event_count();
    std::uint64_t handle_pending_count();

    transport_counter_snapshot counter_snapshot() const;
    void reset_counters();

    /// \brief Register this topic with an event handler without externally draining its eventfd counter.
    bool register_events(event::fd_event_handler& handler) override;

    /// \brief Unregister this topic's event fd from an event handler.
    bool unregister_events(event::fd_event_handler& handler) override;

private:
    topic(ring_buffer rb);

    std::uint64_t handle_subscriber_event(bool drain_wake_fd);
    void flush_ack_batch(std::uint64_t& ack_count);
    void lock_publisher();
    void unlock_publisher();

    ring_buffer m_rb;

    // Control Plane FDs
    std::shared_ptr<event::event_fd_base> m_pub_fd;
    std::shared_ptr<event::event_fd_base> m_release_fd;
    std::shared_ptr<event::event_fd_base> m_broadcast_fd;
    std::shared_ptr<event::event_fd_base> m_ack_fd;

    on_data_view_callback m_callback;
    on_sequence_error_callback m_sequence_error_callback;

    std::uint32_t m_read_idx{0};
    std::uint64_t m_local_release_count{0};
    std::optional<std::uint64_t> m_expected_read_sequence;
    bool m_is_publisher{false};
    /// \brief Coordinator-assigned subscriber id used for progress-table access.
    ///
    /// The subscriber writes its monotonic ACK progress to this index of the
    /// SubscriberAckProgressTable. It also reads the coordinator's dispatched-count from the same
    /// entry to decide how many slots to consume after a wake-up.
    std::uint32_t m_subscriber_id{0};

    /// \brief Local mirror of this subscriber's SHM acked_count.
    ///
    /// Tracks how many slots this topic has already delivered to its callback (and signalled an
    /// ACK for). The handle_subscriber_event loop processes the SHM dispatched_count - this
    /// value slots, then increments both this counter and the SHM acked_count. Keeping a local
    /// copy avoids re-loading the SHM atomic inside the loop.
    std::uint64_t m_local_acked_count{0};
    std::atomic<std::uint64_t> m_messages_published{0};
    std::atomic<std::uint64_t> m_messages_dispatched{0};
    std::atomic<std::uint64_t> m_subscriber_acks_observed{0};
    std::atomic<std::uint64_t> m_slots_reused{0};
    std::atomic<std::uint64_t> m_blocked_publish_attempts{0};
    std::atomic<std::uint64_t> m_dropped_publish_attempts{0};
    std::atomic<std::uint64_t> m_failed_publish_attempts{0};
};

} // namespace everest::lib::io::shm
