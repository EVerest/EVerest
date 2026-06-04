// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <cstddef>
#include <cstdint>
#include <everest/io/shm/ring_buffer.hpp>
#include <stdexcept>

namespace everest::lib::io::shm {

ring_buffer::ring_buffer(void* base_ptr) :
    m_metadata(static_cast<RingbufferMetadata*>(base_ptr)),
    m_ack_progress(
        reinterpret_cast<SubscriberAckProgressTable*>(static_cast<char*>(base_ptr) + sizeof(RingbufferMetadata))),
    m_data_base(static_cast<char*>(base_ptr) + shm_ring_buffer_header_size()) {
}

RingbufferMetadata* ring_buffer::get_metadata() const {
    return m_metadata;
}

ShmSlotHeader* ring_buffer::get_slot_header(std::uint32_t slot_idx) const {
    const auto slot_full_size = static_cast<std::size_t>(shm_ring_buffer_slot_stride(m_metadata->slot_size));
    return reinterpret_cast<ShmSlotHeader*>(m_data_base + (slot_idx * slot_full_size));
}

void* ring_buffer::get_slot_payload(std::uint32_t slot_idx) const {
    ShmSlotHeader* header = get_slot_header(slot_idx);
    return reinterpret_cast<char*>(header) + sizeof(ShmSlotHeader);
}

SubscriberAckProgressTable* ring_buffer::get_subscriber_ack_progress_table() const {
    return m_ack_progress;
}

std::atomic<std::uint64_t>& ring_buffer::subscriber_acked_count(std::uint32_t subscriber_id) const {
    if (subscriber_id >= shm_max_subscribers_per_topic) {
        throw std::out_of_range("SHM subscriber id exceeds per-topic capacity");
    }
    return m_ack_progress->entries[subscriber_id].acked_count;
}

std::atomic<std::uint64_t>& ring_buffer::subscriber_dispatched_count(std::uint32_t subscriber_id) const {
    if (subscriber_id >= shm_max_subscribers_per_topic) {
        throw std::out_of_range("SHM subscriber id exceeds per-topic capacity");
    }
    return m_ack_progress->entries[subscriber_id].dispatched_count;
}

std::atomic<std::uint64_t>& ring_buffer::publication_count() const {
    return m_metadata->publication_count;
}

std::atomic<std::uint64_t>& ring_buffer::release_count() const {
    return m_metadata->release_count;
}

std::size_t ring_buffer::calculate_required_size(std::uint32_t total_slots, std::uint32_t slot_size) {
    return shm_ring_buffer_required_size(total_slots, slot_size);
}

} // namespace everest::lib::io::shm
