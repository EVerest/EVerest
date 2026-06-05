// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <algorithm>
#include <atomic>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <thread>
#include <utility>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/shm/topic.hpp>

namespace everest::lib::io::shm {

topic::topic(ring_buffer rb) : m_rb(rb) {
}

std::unique_ptr<topic> topic::make_publisher(ring_buffer rb, event::unique_fd&& pub_fd, event::unique_fd&& release_fd) {
    return make_publisher(std::move(rb), std::make_shared<event::event_fd_base>(std::move(pub_fd)),
                          std::make_shared<event::event_fd_base>(std::move(release_fd)));
}

std::unique_ptr<topic> topic::make_publisher(ring_buffer rb, std::shared_ptr<event::event_fd_base> pub_fd,
                                             std::shared_ptr<event::event_fd_base> release_fd) {
    auto t = std::unique_ptr<topic>(new topic(rb));
    t->m_pub_fd = std::move(pub_fd);
    t->m_release_fd = std::move(release_fd);
    t->m_is_publisher = true;
    t->m_local_release_count = t->m_rb.release_count().load(std::memory_order_acquire);
    return t;
}

std::unique_ptr<topic> topic::make_subscriber(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                              event::unique_fd&& ack_fd, on_data_callback callback,
                                              std::uint32_t subscriber_id) {
    on_data_view_callback view_callback;
    if (callback) {
        view_callback = [callback = std::move(callback)](std::string_view data) { callback(std::string(data)); };
    }
    return make_subscriber_view(std::move(rb), std::move(broadcast_fd), std::move(ack_fd), std::move(view_callback),
                                nullptr, subscriber_id);
}

std::unique_ptr<topic> topic::make_subscriber(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                              event::unique_fd&& ack_fd, on_data_callback callback,
                                              on_sequence_error_callback sequence_error_callback,
                                              std::uint32_t subscriber_id) {
    on_data_view_callback view_callback;
    if (callback) {
        view_callback = [callback = std::move(callback)](std::string_view data) { callback(std::string(data)); };
    }
    return make_subscriber_view(std::move(rb), std::move(broadcast_fd), std::move(ack_fd), std::move(view_callback),
                                std::move(sequence_error_callback), subscriber_id);
}

std::unique_ptr<topic> topic::make_subscriber_view(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                                   event::unique_fd&& ack_fd, on_data_view_callback callback,
                                                   std::uint32_t subscriber_id) {
    return make_subscriber_view(std::move(rb), std::move(broadcast_fd), std::move(ack_fd), std::move(callback), nullptr,
                                subscriber_id);
}

std::unique_ptr<topic> topic::make_subscriber_view(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                                   event::unique_fd&& ack_fd, on_data_view_callback callback,
                                                   on_sequence_error_callback sequence_error_callback,
                                                   std::uint32_t subscriber_id) {
    auto t = std::unique_ptr<topic>(new topic(rb));
    t->m_broadcast_fd = std::make_shared<event::event_fd_base>(std::move(broadcast_fd));
    t->m_ack_fd = std::make_shared<event::event_fd_base>(std::move(ack_fd));
    t->m_callback = std::move(callback);
    t->m_sequence_error_callback = std::move(sequence_error_callback);
    t->m_is_publisher = false;
    t->m_subscriber_id = subscriber_id;

    // Initialize read_idx to the current write_idx to avoid reading stale data
    t->m_read_idx = t->m_rb.get_metadata()->write_idx.load(std::memory_order_acquire);
    return t;
}

std::unique_ptr<topic> topic::make_subscriber(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                              event::unique_fd&& ack_fd, on_data_callback callback,
                                              subscriber_cursor cursor, std::uint32_t subscriber_id) {
    on_data_view_callback view_callback;
    if (callback) {
        view_callback = [callback = std::move(callback)](std::string_view data) { callback(std::string(data)); };
    }
    return make_subscriber_view(std::move(rb), std::move(broadcast_fd), std::move(ack_fd), std::move(view_callback),
                                cursor, nullptr, subscriber_id);
}

std::unique_ptr<topic> topic::make_subscriber(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                              event::unique_fd&& ack_fd, on_data_callback callback,
                                              subscriber_cursor cursor,
                                              on_sequence_error_callback sequence_error_callback,
                                              std::uint32_t subscriber_id) {
    on_data_view_callback view_callback;
    if (callback) {
        view_callback = [callback = std::move(callback)](std::string_view data) { callback(std::string(data)); };
    }
    return make_subscriber_view(std::move(rb), std::move(broadcast_fd), std::move(ack_fd), std::move(view_callback),
                                cursor, std::move(sequence_error_callback), subscriber_id);
}

std::unique_ptr<topic> topic::make_subscriber_view(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                                   event::unique_fd&& ack_fd, on_data_view_callback callback,
                                                   subscriber_cursor cursor, std::uint32_t subscriber_id) {
    return make_subscriber_view(std::move(rb), std::move(broadcast_fd), std::move(ack_fd), std::move(callback), cursor,
                                nullptr, subscriber_id);
}

std::unique_ptr<topic> topic::make_subscriber_view(ring_buffer rb, event::unique_fd&& broadcast_fd,
                                                   event::unique_fd&& ack_fd, on_data_view_callback callback,
                                                   subscriber_cursor cursor,
                                                   on_sequence_error_callback sequence_error_callback,
                                                   std::uint32_t subscriber_id) {
    auto t = std::unique_ptr<topic>(new topic(rb));
    t->m_broadcast_fd = std::make_shared<event::event_fd_base>(std::move(broadcast_fd));
    t->m_ack_fd = std::make_shared<event::event_fd_base>(std::move(ack_fd));
    t->m_callback = std::move(callback);
    t->m_sequence_error_callback = std::move(sequence_error_callback);
    t->m_is_publisher = false;
    t->m_subscriber_id = subscriber_id;

    if (cursor.write_idx >= t->m_rb.get_metadata()->total_slots) {
        throw std::out_of_range("SHM subscriber cursor write index is out of range");
    }
    t->m_read_idx = cursor.write_idx;
    t->m_expected_read_sequence = cursor.sequence;
    return t;
}

std::unique_ptr<topic> topic::make_subscriber_view(ring_buffer rb, std::shared_ptr<event::event_fd_base> broadcast_fd,
                                                   std::shared_ptr<event::event_fd_base> ack_fd,
                                                   on_data_view_callback callback, subscriber_cursor cursor,
                                                   std::uint32_t subscriber_id) {
    return make_subscriber_view(std::move(rb), std::move(broadcast_fd), std::move(ack_fd), std::move(callback), cursor,
                                nullptr, subscriber_id);
}

std::unique_ptr<topic> topic::make_subscriber_view(ring_buffer rb, std::shared_ptr<event::event_fd_base> broadcast_fd,
                                                   std::shared_ptr<event::event_fd_base> ack_fd,
                                                   on_data_view_callback callback, subscriber_cursor cursor,
                                                   on_sequence_error_callback sequence_error_callback,
                                                   std::uint32_t subscriber_id) {
    auto t = std::unique_ptr<topic>(new topic(rb));
    t->m_broadcast_fd = std::move(broadcast_fd);
    t->m_ack_fd = std::move(ack_fd);
    t->m_callback = std::move(callback);
    t->m_sequence_error_callback = std::move(sequence_error_callback);
    t->m_is_publisher = false;
    t->m_subscriber_id = subscriber_id;

    if (cursor.write_idx >= t->m_rb.get_metadata()->total_slots) {
        throw std::out_of_range("SHM subscriber cursor write index is out of range");
    }
    t->m_read_idx = cursor.write_idx;
    t->m_expected_read_sequence = cursor.sequence;
    return t;
}

bool topic::publish(const std::string& data, bool block) {
    return publish(std::string_view(data), block);
}

bool topic::publish(const char* data, bool block) {
    return publish(std::string_view(data), block);
}

bool topic::publish(std::string_view data, bool block) {
    return publish(data, block ? full_buffer_policy::block_publisher : full_buffer_policy::fail_immediately);
}

bool topic::publish(const std::string& data, full_buffer_policy policy) {
    return publish(std::string_view(data), policy);
}

bool topic::publish(const char* data, full_buffer_policy policy) {
    return publish(std::string_view(data), policy);
}

bool topic::publish(std::string_view data, full_buffer_policy policy) {
    return publish(data, policy, false, false);
}

bool topic::publish(std::string_view data, full_buffer_policy policy, bool retain, bool clear_retained) {
    auto* meta = m_rb.get_metadata();
    if (data.size() > meta->slot_size) {
        m_failed_publish_attempts.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    lock_publisher();
    struct publisher_unlock_guard {
        topic& owner;
        ~publisher_unlock_guard() {
            owner.unlock_publisher();
        }
    } unlock_guard{*this};

    if (meta->target_subscribers.load(std::memory_order_acquire) == 0 && !retain && !clear_retained) {
        m_dropped_publish_attempts.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    uint32_t current_idx = meta->write_idx.load(std::memory_order_acquire);
    auto* slot_header = m_rb.get_slot_header(current_idx);
    const bool reused_slot = slot_header->sequence != 0;

    // Check if slot is free (all subscribers have acknowledged)
    bool blocked = false;
    while (slot_header->ack_count.load(std::memory_order_acquire) <
           slot_header->target_subscribers.load(std::memory_order_acquire)) {
        if (!blocked) {
            m_blocked_publish_attempts.fetch_add(1, std::memory_order_relaxed);
            blocked = true;
        }
        if (policy == full_buffer_policy::fail_immediately) {
            m_failed_publish_attempts.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (handle_pending_count() == 0) {
            pollfd release_poll{};
            release_poll.fd = m_release_fd->get_raw_fd();
            release_poll.events = POLLIN;
            const auto ready = ::poll(&release_poll, 1, -1);
            if (ready > 0 && handle_pending_count() == 0) {
                // The wake descriptor may be shared across topics. Do not consume an
                // unrelated wake credit; yield and re-check this topic's SHM release counter.
                std::this_thread::yield();
            }
        }
    }

    // Write payload
    size_t len = data.size();
    std::memcpy(m_rb.get_slot_payload(current_idx), data.data(), len);
    slot_header->data_length = static_cast<uint32_t>(len);
    slot_header->flags = (retain ? shm_slot_flag_retain : 0U) | (clear_retained ? shm_slot_flag_clear_retained : 0U);
    slot_header->sequence = meta->next_sequence.fetch_add(1, std::memory_order_acq_rel);
    // The coordinator stamps this slot's subscriber target during dispatch.

    // Reset ACK count (Release barrier ensures data write is visible)
    slot_header->ack_count.store(0, std::memory_order_release);

    // Move to next slot
    meta->write_idx.store((current_idx + 1) % meta->total_slots, std::memory_order_release);

    // Notify Manager
    m_rb.publication_count().fetch_add(1, std::memory_order_acq_rel);
    m_pub_fd->notify();

    if (reused_slot) {
        m_slots_reused.fetch_add(1, std::memory_order_relaxed);
    }
    m_messages_published.fetch_add(1, std::memory_order_relaxed);

    return true;
}

void topic::lock_publisher() {
    auto* meta = m_rb.get_metadata();
    std::uint32_t expected = 0;
    while (
        !meta->publish_lock.compare_exchange_weak(expected, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
        expected = 0;
        std::this_thread::yield();
    }
}

void topic::unlock_publisher() {
    m_rb.get_metadata()->publish_lock.store(0, std::memory_order_release);
}

event::event_fd_base* topic::get_event_fd() {
    if (m_is_publisher) {
        return m_release_fd.get();
    } else {
        return m_broadcast_fd.get();
    }
}

void topic::handle_event() {
    (void)handle_event_count();
}

std::uint64_t topic::handle_event_count() {
    if (m_is_publisher) {
        (void)m_release_fd->read();
        return handle_pending_count();
    }

    return handle_subscriber_event(true);
}

std::uint64_t topic::handle_pending_count() {
    if (m_is_publisher) {
        const auto release_count = m_rb.release_count().load(std::memory_order_acquire);
        if (release_count < m_local_release_count) {
            m_local_release_count = release_count;
            return 0;
        }
        const auto pending = release_count - m_local_release_count;
        m_local_release_count = release_count;
        return pending;
    }
    return handle_subscriber_event(false);
}

std::uint64_t topic::handle_subscriber_event(bool drain_wake_fd) {
    auto* meta = m_rb.get_metadata();
    // Consume one wake notification. In production this is the per-client session wake FD
    // duplicated by the control server; direct coordinator tests may still use the legacy
    // coordinator-owned topic wake FD. The actual "how many slots to process" value comes from
    // the SHM-resident per-subscriber dispatched_count delta computed below.
    if (drain_wake_fd && m_broadcast_fd != nullptr) {
        (void)m_broadcast_fd->read();
    }

    const auto dispatched_count = m_rb.subscriber_dispatched_count(m_subscriber_id).load(std::memory_order_acquire);
    if (dispatched_count < m_local_acked_count) {
        // Subscriber slot was just recycled by the coordinator (counters reset to zero) while
        // our local copy still holds the previous registration's value. Resynchronize and
        // bail; the next genuine wake-up will deliver any newly dispatched slots.
        m_local_acked_count = dispatched_count;
        return 0;
    }
    const auto total_slots = meta->total_slots;
    std::uint64_t pending_acks = 0;
    try {
        while (m_local_acked_count < dispatched_count) {
            auto* header = m_rb.get_slot_header(m_read_idx);
            const char* payload = static_cast<const char*>(m_rb.get_slot_payload(m_read_idx));
            const auto actual_sequence = header->sequence;
            auto status = sequence_status::accepted;
            auto expected_sequence = actual_sequence;

            if (m_expected_read_sequence.has_value()) {
                expected_sequence = m_expected_read_sequence.value();
                if (actual_sequence < expected_sequence) {
                    status = sequence_status::stale;
                } else if (actual_sequence > expected_sequence) {
                    status = sequence_status::gap;
                }
            }

            if (status == sequence_status::accepted && m_callback) {
                m_callback(std::string_view(payload, header->data_length));
            } else if (status != sequence_status::accepted && m_sequence_error_callback) {
                m_sequence_error_callback({status, expected_sequence, actual_sequence, m_read_idx});
            }
            m_expected_read_sequence = actual_sequence + 1;

            ++pending_acks;
            ++m_local_acked_count;
            m_read_idx = (m_read_idx + 1) % total_slots;
        }
    } catch (...) {
        flush_ack_batch(pending_acks);
        throw;
    }

    const auto delivered_count = pending_acks;
    flush_ack_batch(pending_acks);
    m_messages_dispatched.fetch_add(delivered_count, std::memory_order_relaxed);
    return delivered_count;
}

void topic::flush_ack_batch(std::uint64_t& ack_count) {
    if (ack_count > 0) {
        // Publish our acked-slot count to the coordinator before signalling the shared per-topic
        // ACK eventfd. The fetch_add memory order ensures the coordinator observes the bump as soon
        // as it processes the eventfd wake-up.
        m_rb.subscriber_acked_count(m_subscriber_id).fetch_add(ack_count, std::memory_order_acq_rel);
        if (m_ack_fd->write(ack_count)) {
            m_subscriber_acks_observed.fetch_add(ack_count, std::memory_order_relaxed);
        }
        ack_count = 0;
    }
}

transport_counter_snapshot topic::counter_snapshot() const {
    transport_counter_snapshot snapshot;
    snapshot.messages_published = m_messages_published.load(std::memory_order_relaxed);
    snapshot.messages_dispatched = m_messages_dispatched.load(std::memory_order_relaxed);
    snapshot.subscriber_acks_observed = m_subscriber_acks_observed.load(std::memory_order_relaxed);
    snapshot.slots_reused = m_slots_reused.load(std::memory_order_relaxed);
    snapshot.blocked_publish_attempts = m_blocked_publish_attempts.load(std::memory_order_relaxed);
    snapshot.dropped_publish_attempts = m_dropped_publish_attempts.load(std::memory_order_relaxed);
    snapshot.failed_publish_attempts = m_failed_publish_attempts.load(std::memory_order_relaxed);
    return snapshot;
}

void topic::reset_counters() {
    m_messages_published.store(0, std::memory_order_relaxed);
    m_messages_dispatched.store(0, std::memory_order_relaxed);
    m_subscriber_acks_observed.store(0, std::memory_order_relaxed);
    m_slots_reused.store(0, std::memory_order_relaxed);
    m_blocked_publish_attempts.store(0, std::memory_order_relaxed);
    m_dropped_publish_attempts.store(0, std::memory_order_relaxed);
    m_failed_publish_attempts.store(0, std::memory_order_relaxed);
}

bool topic::register_events(event::fd_event_handler& handler) {
    auto* event_fd = get_event_fd();
    if (event_fd == nullptr) {
        return false;
    }

    return handler.register_event_handler(
        event_fd->get_raw_fd(), [this](const auto&) { handle_event(); }, event::poll_events::read);
}

bool topic::unregister_events(event::fd_event_handler& handler) {
    auto* event_fd = get_event_fd();
    if (event_fd == nullptr) {
        return false;
    }

    return handler.unregister_event_handler(event_fd->get_raw_fd());
}

} // namespace everest::lib::io::shm
