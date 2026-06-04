// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/io/shm/coordinator.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace everest::lib::io::shm {

namespace {
void validate_slot_index(const RingbufferMetadata& meta, std::uint32_t slot_idx) {
    if (slot_idx >= meta.total_slots) {
        throw std::out_of_range("SHM slot index out of range");
    }
}

void lock_publish_state(RingbufferMetadata& meta) {
    std::uint32_t expected = 0;
    while (
        !meta.publish_lock.compare_exchange_weak(expected, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
        expected = 0;
        std::this_thread::yield();
    }
}

void unlock_publish_state(RingbufferMetadata& meta) {
    meta.publish_lock.store(0, std::memory_order_release);
}

struct publish_state_lock_guard {
    explicit publish_state_lock_guard(RingbufferMetadata& meta_) : meta(meta_) {
        lock_publish_state(meta);
    }

    ~publish_state_lock_guard() {
        unlock_publish_state(meta);
    }

    RingbufferMetadata& meta;
};
} // namespace

coordinator::coordinator(ring_buffer rb) :
    coordinator(rb, notification_fds{std::make_shared<event::event_fd>(), std::make_shared<event::event_fd>(),
                                     std::make_shared<event::event_fd>(), std::make_shared<event::semaphore_fd>()}) {
}

coordinator::coordinator(ring_buffer rb, notification_fds fds) :
    m_rb(rb),
    m_publication_fd(std::move(fds.publication)),
    m_release_fd(std::move(fds.release)),
    m_topic_ack_fd(std::move(fds.ack)),
    m_topic_broadcast_fd(std::move(fds.broadcast)),
    m_dispatch_idx(rb.get_metadata()->write_idx.load(std::memory_order_acquire)),
    m_last_observed_publication_count(rb.publication_count().load(std::memory_order_acquire)) {
    if (m_publication_fd == nullptr || m_release_fd == nullptr || m_topic_ack_fd == nullptr ||
        m_topic_broadcast_fd == nullptr) {
        throw std::invalid_argument("SHM coordinator notification FDs must be set");
    }
}

void coordinator::initialize_ring_buffer(ring_buffer rb, std::uint32_t total_slots, std::uint32_t slot_size,
                                         std::uint32_t target_subscribers) {
    if (total_slots == 0) {
        throw std::invalid_argument("SHM ringbuffer needs at least one slot");
    }
    if (slot_size == 0) {
        throw std::invalid_argument("SHM ringbuffer slot size must be non-zero");
    }

    auto* meta = rb.get_metadata();
    meta->total_slots = total_slots;
    meta->slot_size = slot_size;
    meta->target_subscribers.store(target_subscribers, std::memory_order_release);
    meta->write_idx.store(0, std::memory_order_release);
    meta->publication_count.store(0, std::memory_order_release);
    meta->release_count.store(0, std::memory_order_release);
    meta->next_sequence.store(1, std::memory_order_release);
    meta->publish_lock.store(0, std::memory_order_release);

    auto* progress_table = rb.get_subscriber_ack_progress_table();
    for (std::uint32_t id = 0; id < shm_max_subscribers_per_topic; ++id) {
        progress_table->entries[id].acked_count.store(0, std::memory_order_release);
        progress_table->entries[id].dispatched_count.store(0, std::memory_order_release);
    }

    for (std::uint32_t slot_idx = 0; slot_idx < total_slots; ++slot_idx) {
        auto* header = rb.get_slot_header(slot_idx);
        header->sequence = 0;
        header->data_length = 0;
        header->flags = 0;
        header->target_subscribers.store(target_subscribers, std::memory_order_release);
        header->ack_count.store(target_subscribers, std::memory_order_release);
        std::memset(rb.get_slot_payload(slot_idx), 0, slot_size);
    }
}

event::unique_fd coordinator::make_publication_fd() const {
    return duplicate_fd(m_publication_fd->get_raw_fd());
}

event::unique_fd coordinator::make_release_fd() const {
    return duplicate_fd(m_release_fd->get_raw_fd());
}

coordinator::subscriber_registration coordinator::add_subscriber() {
    auto* meta = m_rb.get_metadata();
    publish_state_lock_guard publish_lock(*meta);
    const auto current_target = meta->target_subscribers.load(std::memory_order_acquire);
    const auto active_registrations = static_cast<std::uint32_t>(
        std::count_if(m_subscribers.begin(), m_subscribers.end(), [](const auto& sub) { return !sub.removed; }));
    const bool dynamic_add = active_registrations >= current_target;
    subscriber_registration registration;
    registration.cursor.write_idx = meta->write_idx.load(std::memory_order_acquire);
    registration.cursor.sequence = meta->next_sequence.load(std::memory_order_acquire);

    auto removed_sub =
        std::find_if(m_subscribers.begin(), m_subscribers.end(), [](const auto& sub) { return sub.removed; });
    subscriber_state* sub = nullptr;
    if (removed_sub != m_subscribers.end()) {
        registration.id = static_cast<subscriber_id>(std::distance(m_subscribers.begin(), removed_sub));
        sub = &*removed_sub;
        sub->pending_slots.clear();
        sub->max_observed_pending_slots = 0;
        sub->acked_slots = 0;
        sub->last_ack_progress_sequence = 0;
        sub->last_observed_ack_progress = 0;
        sub->join_sequence = registration.cursor.sequence;
        sub->active = true;
        sub->pending_activation = false;
        sub->removed = false;
    } else {
        registration.id = m_subscribers.size();
        if (registration.id >= shm_max_subscribers_per_topic) {
            throw std::runtime_error("SHM coordinator subscriber count exceeds per-topic capacity");
        }
        sub = &m_subscribers.emplace_back();
    }
    sub->join_sequence = registration.cursor.sequence;

    // Reset the SHM-visible per-subscriber progress so the subscriber-side topic starts
    // counting from zero. dispatched_count must also be reset because the coordinator bumps
    // it on every dispatch and the subscriber compares delta against acked_count.
    m_rb.subscriber_acked_count(static_cast<std::uint32_t>(registration.id)).store(0, std::memory_order_release);
    m_rb.subscriber_dispatched_count(static_cast<std::uint32_t>(registration.id)).store(0, std::memory_order_release);

    if (dynamic_add) {
        if (all_slots_released()) {
            const auto new_target = meta->target_subscribers.fetch_add(1, std::memory_order_acq_rel) + 1;
            retarget_released_slots(new_target);
        } else {
            sub->active = false;
            sub->pending_activation = true;
            ++m_pending_activation_count;
            registration.state = subscriber_join_state::pending;
        }
    }

    m_subscriber_joins.fetch_add(1, std::memory_order_relaxed);
    return registration;
}

bool coordinator::remove_subscriber(subscriber_id id) {
    auto& sub = subscriber(id);
    if (sub.removed) {
        return false;
    }

    auto* meta = m_rb.get_metadata();
    if (sub.active && meta->target_subscribers.load(std::memory_order_acquire) == 0) {
        throw std::runtime_error("Cannot remove SHM subscriber from an empty subscriber set");
    }

    if (sub.active) {
        meta->target_subscribers.fetch_sub(1, std::memory_order_acq_rel);
    } else if (sub.pending_activation) {
        --m_pending_activation_count;
    }
    sub.active = false;
    sub.pending_activation = false;
    sub.removed = true;

    while (!sub.pending_slots.empty()) {
        const auto slot_idx = sub.pending_slots.front();
        sub.pending_slots.pop_front();
        release_removed_subscriber_slot(slot_idx);
    }
    activate_pending_subscribers();

    m_subscriber_removals.fetch_add(1, std::memory_order_relaxed);
    return true;
}

event::unique_fd coordinator::make_broadcast_fd(subscriber_id id) const {
    const auto& sub = subscriber(id);
    if (sub.removed) {
        throw std::runtime_error("Cannot create broadcast FD for removed SHM subscriber");
    }
    return duplicate_fd(m_topic_broadcast_fd->get_raw_fd());
}

event::unique_fd coordinator::make_topic_broadcast_fd() const {
    return duplicate_fd(m_topic_broadcast_fd->get_raw_fd());
}

event::unique_fd coordinator::make_ack_fd(subscriber_id id) const {
    const auto& sub = subscriber(id);
    if (sub.removed) {
        throw std::runtime_error("Cannot create ACK FD for removed SHM subscriber");
    }
    return duplicate_fd(m_topic_ack_fd->get_raw_fd());
}

event::unique_fd coordinator::make_topic_ack_fd() const {
    return duplicate_fd(m_topic_ack_fd->get_raw_fd());
}

std::size_t coordinator::handle_publication() {
    return handle_publication(nullptr);
}

std::size_t coordinator::handle_publication(publication_callback callback) {
    return handle_publication(std::move(callback), nullptr);
}

std::size_t coordinator::handle_publication(publication_callback callback,
                                            subscriber_dispatch_callback dispatch_callback) {
    const auto publication_signal_count = m_publication_fd->read();
    if (!publication_signal_count.has_value()) {
        return 0;
    }
    return handle_pending_publication(std::move(callback), std::move(dispatch_callback));
}

std::size_t coordinator::handle_pending_publication(publication_callback callback) {
    return handle_pending_publication(std::move(callback), nullptr);
}

std::size_t coordinator::handle_pending_publication(publication_callback callback,
                                                    subscriber_dispatch_callback dispatch_callback) {
    const auto current_publication_count = m_rb.publication_count().load(std::memory_order_acquire);
    if (current_publication_count < m_last_observed_publication_count) {
        m_last_observed_publication_count = current_publication_count;
        return 0;
    }
    const auto publication_count = current_publication_count - m_last_observed_publication_count;
    if (publication_count == 0) {
        return 0;
    }
    m_last_observed_publication_count = current_publication_count;

    auto* meta = m_rb.get_metadata();
    std::size_t dispatched = 0;
    std::uint64_t subscriber_dispatches = 0;
    std::uint32_t active_subscriber_count = 0;
    // Per-subscriber tally of newly-dispatched slots so we can publish a single fetch_add
    // to each subscriber's SHM dispatched_count after the dispatch loop completes. A
    // per-slot RMW would cost N (subscribers) atomics per slot — for a batch of K slots
    // that is N*K cache-line round trips even though the subscriber-visible state is
    // identical to a single per-subscriber RMW of K at the end.
    std::array<std::uint32_t, shm_max_subscribers_per_topic> per_subscriber_dispatches{};

    while (dispatched < publication_count) {
        validate_slot_index(*meta, m_dispatch_idx);
        auto* header = m_rb.get_slot_header(m_dispatch_idx);
        if (callback) {
            const auto* payload = static_cast<const char*>(m_rb.get_slot_payload(m_dispatch_idx));
            callback(std::string_view(payload, header->data_length), (header->flags & shm_slot_flag_retain) != 0U,
                     (header->flags & shm_slot_flag_clear_retained) != 0U);
        }
        std::uint32_t targeted_subscribers = 0;

        for (subscriber_id id = 0; id < m_subscribers.size(); ++id) {
            auto& sub = m_subscribers[id];
            if (sub.active && header->sequence >= sub.join_sequence) {
                sub.pending_slots.push_back(m_dispatch_idx);
                sub.max_observed_pending_slots =
                    std::max<std::uint64_t>(sub.max_observed_pending_slots, sub.pending_slots.size());
                ++per_subscriber_dispatches[id];
                ++targeted_subscribers;
            }
        }
        header->target_subscribers.store(targeted_subscribers, std::memory_order_release);
        subscriber_dispatches += targeted_subscribers;
        if (targeted_subscribers == 0) {
            release_if_complete(m_dispatch_idx);
        }
        m_dispatch_idx = (m_dispatch_idx + 1) % meta->total_slots;
        ++dispatched;
    }
    if (dispatched > 0) {
        // Single per-subscriber fetch_add covering the whole batch. The release ordering
        // pairs with the subscriber-side acquire load on dispatched_count. Production callers
        // provide dispatch_callback and wake affected client sessions after these counters are
        // visible; direct coordinator tests fall back to the legacy topic wake below.
        for (subscriber_id id = 0; id < m_subscribers.size(); ++id) {
            if (m_subscribers[id].active) {
                ++active_subscriber_count;
                const auto delta = per_subscriber_dispatches[id];
                if (delta > 0) {
                    m_rb.subscriber_dispatched_count(static_cast<std::uint32_t>(id))
                        .fetch_add(delta, std::memory_order_acq_rel);
                    if (dispatch_callback) {
                        dispatch_callback(id);
                    }
                }
            }
        }
        if (active_subscriber_count > 0 && !dispatch_callback) {
            // Legacy direct-topic test path. Production SHM server dispatch uses the callback
            // above to wake per-client session FDs instead of this shared topic semaphore.
            if (!m_topic_broadcast_fd->write(active_subscriber_count)) {
                m_failed_dispatch_attempts.fetch_add(subscriber_dispatches, std::memory_order_relaxed);
            }
        }
    }
    activate_pending_subscribers();

    m_messages_published.fetch_add(dispatched, std::memory_order_relaxed);
    m_messages_dispatched.fetch_add(subscriber_dispatches, std::memory_order_relaxed);

    return dispatched;
}

bool coordinator::handle_ack(subscriber_id id) {
    return handle_ack_details(id).released;
}

coordinator::ack_result coordinator::handle_ack_details(subscriber_id id) {
    auto& sub = subscriber(id);
    if (!sub.active) {
        return {};
    }
    return handle_ack_for_subscriber(sub, id);
}

coordinator::ack_result coordinator::handle_topic_ack() {
    const auto signal_count = m_topic_ack_fd->read();
    (void)signal_count;
    return handle_pending_acks();
}

coordinator::ack_result coordinator::handle_pending_acks() {
    ack_result aggregate;
    for (subscriber_id id = 0; id < m_subscribers.size(); ++id) {
        auto& sub = m_subscribers[id];
        if (!sub.active) {
            continue;
        }
        const auto result = handle_ack_for_subscriber(sub, id);
        aggregate.ack_count += result.ack_count;
        aggregate.released = result.released || aggregate.released;
    }
    return aggregate;
}

coordinator::ack_result coordinator::handle_ack_for_subscriber(subscriber_state& sub, subscriber_id id) {
    const auto current_progress =
        m_rb.subscriber_acked_count(static_cast<std::uint32_t>(id)).load(std::memory_order_acquire);
    if (current_progress < sub.last_observed_ack_progress) {
        // add_subscriber() resets the SHM acked_count and last_observed_ack_progress to zero in
        // lock-step before any new ACKs can arrive, so a legitimate rollback should not be
        // reachable. If we hit it anyway it points to either (a) cross-process memory
        // corruption of the SHM progress table or (b) a stale ACK arriving from a previous
        // subscriber on a recycled id. Surface the anomaly rather than silently sliding
        // last_observed backwards. Rebase last_observed to the (lower) current_progress so the
        // coordinator keeps tracking the live value instead of computing a giant bogus delta
        // on the next ACK, but log a critical error first so an operator can investigate.
        std::cerr << "SHM coordinator: subscriber " << id << " acked_count went backwards "
                  << "(observed=" << sub.last_observed_ack_progress << ", current=" << current_progress
                  << ") — possible SHM corruption or stale ack from a recycled subscriber id\n";
        sub.last_observed_ack_progress = current_progress;
        return {};
    }
    const auto delta = current_progress - sub.last_observed_ack_progress;
    if (delta == 0) {
        return {};
    }
    sub.last_observed_ack_progress = current_progress;

    ack_result result;
    result.ack_count = delta;
    m_subscriber_acks_observed.fetch_add(delta, std::memory_order_relaxed);
    for (std::uint64_t ack = 0; ack < delta; ++ack) {
        if (sub.pending_slots.empty()) {
            std::cerr << "Received SHM ACK without a pending dispatched slot for subscriber " << id << "\n";
            continue;
        }

        const auto slot_idx = sub.pending_slots.front();
        sub.pending_slots.pop_front();
        ++sub.acked_slots;
        sub.last_ack_progress_sequence = m_ack_progress_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
        const auto released = acknowledge_slot(slot_idx);
        if (released) {
            m_slots_released.fetch_add(1, std::memory_order_relaxed);
        }
        result.released = released || result.released;
    }
    activate_pending_subscribers();

    return result;
}

event::event_fd_base* coordinator::publication_event_fd() {
    return m_publication_fd.get();
}

event::event_fd_base* coordinator::topic_broadcast_event_fd() {
    return m_topic_broadcast_fd.get();
}

event::event_fd_base* coordinator::topic_ack_event_fd() {
    return m_topic_ack_fd.get();
}

event::event_fd_base* coordinator::ack_event_fd(subscriber_id id) {
    (void)subscriber(id);
    return m_topic_ack_fd.get();
}

coordinator::subscriber_join_state coordinator::subscriber_join_status(subscriber_id id) const {
    const auto& sub = subscriber(id);
    if (sub.removed) {
        throw std::runtime_error("Cannot query join status for removed SHM subscriber");
    }
    if (sub.pending_activation) {
        return subscriber_join_state::pending;
    }
    return subscriber_join_state::active;
}

transport_counter_snapshot coordinator::counter_snapshot() const {
    transport_counter_snapshot snapshot;
    snapshot.messages_published = m_messages_published.load(std::memory_order_relaxed);
    snapshot.messages_dispatched = m_messages_dispatched.load(std::memory_order_relaxed);
    snapshot.subscriber_acks_observed = m_subscriber_acks_observed.load(std::memory_order_relaxed);
    snapshot.slots_released = m_slots_released.load(std::memory_order_relaxed);
    snapshot.failed_dispatch_attempts = m_failed_dispatch_attempts.load(std::memory_order_relaxed);
    snapshot.subscriber_joins = m_subscriber_joins.load(std::memory_order_relaxed);
    snapshot.subscriber_removals = m_subscriber_removals.load(std::memory_order_relaxed);
    return snapshot;
}

std::vector<coordinator::subscriber_backpressure_snapshot> coordinator::subscriber_backpressure_snapshots() const {
    std::vector<subscriber_backpressure_snapshot> snapshots;
    snapshots.reserve(m_subscribers.size());
    for (subscriber_id id = 0; id < m_subscribers.size(); ++id) {
        const auto& sub = m_subscribers[id];
        snapshots.push_back(subscriber_backpressure_snapshot{
            id, sub.pending_slots.size(), sub.max_observed_pending_slots, sub.acked_slots,
            sub.last_ack_progress_sequence, sub.active, sub.pending_activation, sub.removed});
    }
    return snapshots;
}

void coordinator::reset_counters() {
    m_messages_published.store(0, std::memory_order_relaxed);
    m_messages_dispatched.store(0, std::memory_order_relaxed);
    m_subscriber_acks_observed.store(0, std::memory_order_relaxed);
    m_slots_released.store(0, std::memory_order_relaxed);
    m_failed_dispatch_attempts.store(0, std::memory_order_relaxed);
    m_subscriber_joins.store(0, std::memory_order_relaxed);
    m_subscriber_removals.store(0, std::memory_order_relaxed);
    m_ack_progress_sequence.store(0, std::memory_order_relaxed);
    for (auto& sub : m_subscribers) {
        sub.max_observed_pending_slots = sub.pending_slots.size();
        sub.acked_slots = 0;
        sub.last_ack_progress_sequence = 0;
    }
}

event::unique_fd coordinator::duplicate_fd(int fd) {
    const int duplicated_fd = ::dup(fd);
    if (duplicated_fd == event::unique_fd::NO_DESCRIPTOR_SENTINEL) {
        throw std::runtime_error("Failed to duplicate SHM coordinator eventfd");
    }
    return event::unique_fd(duplicated_fd);
}

coordinator::subscriber_state& coordinator::subscriber(subscriber_id id) {
    if (id >= m_subscribers.size()) {
        throw std::out_of_range("SHM subscriber id out of range");
    }
    return m_subscribers.at(id);
}

const coordinator::subscriber_state& coordinator::subscriber(subscriber_id id) const {
    if (id >= m_subscribers.size()) {
        throw std::out_of_range("SHM subscriber id out of range");
    }
    return m_subscribers.at(id);
}

bool coordinator::all_slots_released() const {
    const auto* meta = m_rb.get_metadata();
    for (std::uint32_t slot_idx = 0; slot_idx < meta->total_slots; ++slot_idx) {
        const auto* header = m_rb.get_slot_header(slot_idx);
        const auto target_subscribers = header->target_subscribers.load(std::memory_order_acquire);
        if (header->ack_count.load(std::memory_order_acquire) < target_subscribers) {
            return false;
        }
    }

    return true;
}

void coordinator::retarget_released_slots(std::uint32_t target_subscribers) {
    const auto* meta = m_rb.get_metadata();
    for (std::uint32_t slot_idx = 0; slot_idx < meta->total_slots; ++slot_idx) {
        auto* header = m_rb.get_slot_header(slot_idx);
        const auto old_target = header->target_subscribers.load(std::memory_order_acquire);
        if (header->ack_count.load(std::memory_order_acquire) >= old_target) {
            header->target_subscribers.store(target_subscribers, std::memory_order_release);
            header->ack_count.store(target_subscribers, std::memory_order_release);
        }
    }
}

void coordinator::activate_pending_subscribers() {
    if (m_pending_activation_count == 0) {
        return;
    }
    if (!all_slots_released()) {
        return;
    }

    auto* meta = m_rb.get_metadata();
    std::uint32_t activated = 0;
    for (auto& sub : m_subscribers) {
        if (sub.pending_activation && !sub.removed) {
            sub.active = true;
            sub.pending_activation = false;
            ++activated;
        }
    }
    if (activated == 0) {
        return;
    }

    m_pending_activation_count -= activated;
    const auto new_target = meta->target_subscribers.fetch_add(activated, std::memory_order_acq_rel) + activated;
    retarget_released_slots(new_target);
}

bool coordinator::acknowledge_slot(std::uint32_t slot_idx) {
    auto* meta = m_rb.get_metadata();
    validate_slot_index(*meta, slot_idx);

    auto* header = m_rb.get_slot_header(slot_idx);
    const auto previous = header->ack_count.fetch_add(1, std::memory_order_acq_rel);
    const auto acknowledged = previous + 1;
    const auto target_subscribers = header->target_subscribers.load(std::memory_order_acquire);

    if (acknowledged == target_subscribers) {
        if (slot_idx == meta->write_idx.load(std::memory_order_acquire)) {
            m_rb.release_count().fetch_add(1, std::memory_order_acq_rel);
            m_release_fd->notify();
        }
        return true;
    }

    if (acknowledged > target_subscribers) {
        throw std::runtime_error("SHM slot ACK count exceeded target subscriber count");
    }

    return false;
}

void coordinator::release_removed_subscriber_slot(std::uint32_t slot_idx) {
    auto* meta = m_rb.get_metadata();
    validate_slot_index(*meta, slot_idx);

    auto* header = m_rb.get_slot_header(slot_idx);
    const auto target_subscribers = header->target_subscribers.load(std::memory_order_acquire);
    if (target_subscribers == 0) {
        return;
    }

    header->target_subscribers.store(target_subscribers - 1, std::memory_order_release);
    if (release_if_complete(slot_idx)) {
        m_slots_released.fetch_add(1, std::memory_order_relaxed);
    }
}

bool coordinator::release_if_complete(std::uint32_t slot_idx) {
    auto* meta = m_rb.get_metadata();
    validate_slot_index(*meta, slot_idx);

    auto* header = m_rb.get_slot_header(slot_idx);
    const auto target_subscribers = header->target_subscribers.load(std::memory_order_acquire);
    if (header->ack_count.load(std::memory_order_acquire) >= target_subscribers) {
        if (slot_idx == meta->write_idx.load(std::memory_order_acquire)) {
            m_rb.release_count().fetch_add(1, std::memory_order_acq_rel);
            m_release_fd->notify();
        }
        return true;
    }

    return false;
}

} // namespace everest::lib::io::shm
