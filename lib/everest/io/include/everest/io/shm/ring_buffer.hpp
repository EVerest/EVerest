// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <cstddef>
#include <cstdint>
#include <everest/io/shm/structures.hpp>

namespace everest::lib::io::shm {

/// \brief Non-owning view of one topic ring in a mapped shared-memory segment.
///
/// The view starts at RingbufferMetadata and provides typed access to metadata,
/// per-subscriber progress entries, slot headers, and slot payload storage.
class ring_buffer {
public:
    /// \brief Construct from a pointer to the start of the ringbuffer metadata.
    explicit ring_buffer(void* base_ptr);

    /// \returns The metadata for this ringbuffer.
    RingbufferMetadata* get_metadata() const;

    /// \returns The header for a specific slot.
    ShmSlotHeader* get_slot_header(std::uint32_t slot_idx) const;

    /// \returns The payload area for a specific slot.
    void* get_slot_payload(std::uint32_t slot_idx) const;

    /// \returns The per-subscriber ACK progress table for this topic.
    SubscriberAckProgressTable* get_subscriber_ack_progress_table() const;

    /// \returns The acked-count atomic for a specific subscriber id.
    /// \throws std::out_of_range when subscriber_id exceeds shm_max_subscribers_per_topic.
    std::atomic<std::uint64_t>& subscriber_acked_count(std::uint32_t subscriber_id) const;

    /// \returns The dispatched-count atomic for a specific subscriber id.
    /// The coordinator bumps this counter every time it commits a slot to the subscriber's
    /// pending queue; the subscriber side reads it to learn how many slots are now ready
    /// for it to consume without depending on wake eventfd counter semantics.
    /// \throws std::out_of_range when subscriber_id exceeds shm_max_subscribers_per_topic.
    std::atomic<std::uint64_t>& subscriber_dispatched_count(std::uint32_t subscriber_id) const;

    /// \returns The monotonic per-topic publication counter.
    std::atomic<std::uint64_t>& publication_count() const;
    /// \returns The monotonic per-topic release counter.
    std::atomic<std::uint64_t>& release_count() const;

    /// \brief Utility to calculate total memory needed for a ringbuffer.
    static std::size_t calculate_required_size(std::uint32_t total_slots, std::uint32_t slot_size);

private:
    RingbufferMetadata* m_metadata;
    SubscriberAckProgressTable* m_ack_progress;
    char* m_data_base;
};

} // namespace everest::lib::io::shm
