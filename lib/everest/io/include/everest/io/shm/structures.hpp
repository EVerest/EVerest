// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace everest::lib::io::shm {

/// \brief Magic marker for EVerest SHM transport segments ("EVSHMSEG" little endian).
constexpr std::uint64_t shm_segment_magic = 0x4745534d48535645ULL;
/// \brief Layout version for the fixed SHM segment ABI.
///
/// This is a hard compatibility boundary. The current layout contains the segment
/// header, fixed-capacity topic registry, per-topic ring metadata, per-subscriber
/// dispatch/ACK progress table, slot headers, and slot payloads. Readers must call
/// validate_segment_header() before trusting any offset stored in the segment.
constexpr std::uint32_t shm_segment_layout_version = 4U;
/// \brief Alignment used by the segment registry.
constexpr std::uint64_t shm_segment_registry_alignment = 8U;
/// \brief Cache-line size assumed for SHM progress structures.
constexpr std::uint64_t shm_cache_line_size = 64U;
/// \brief Alignment for each topic ring inside the segment.
constexpr std::uint64_t shm_topic_ring_alignment = shm_cache_line_size;
/// \brief Topic registry entry flag marking an initialized topic.
constexpr std::uint32_t shm_topic_registry_entry_active = 1U;
/// \brief Slot flag marking a publication as retained.
constexpr std::uint32_t shm_slot_flag_retain = 1U << 0U;
/// \brief Slot flag clearing the retained payload for a topic.
constexpr std::uint32_t shm_slot_flag_clear_retained = 1U << 1U;
/// \brief Inline topic-name capacity in a registry entry, including terminator space.
constexpr std::uint32_t shm_topic_name_capacity = 256U;
/// \brief Fixed size of a topic registry entry in bytes.
constexpr std::uint64_t shm_segment_registry_entry_size = 320U;

/// \brief Validation status for an EVerest SHM transport segment header.
enum class SegmentHeaderValidationStatus : std::uint32_t {
    valid = 0,
    null_segment,
    mapped_segment_too_small,
    bad_magic,
    incompatible_version,
    segment_size_smaller_than_header,
    segment_size_larger_than_mapping,
    registry_offset_before_header,
    registry_offset_outside_segment,
    registry_metadata_outside_segment,
    registry_offset_misaligned,
    registry_entry_count_exceeds_capacity,
};

/// \brief Validation status for a fixed-layout topic registry entry.
enum class TopicRegistryValidationStatus : std::uint32_t {
    valid = 0,
    null_segment,
    null_header,
    null_entry,
    invalid_segment_header,
    free_entry,
    empty_topic_name,
    topic_name_too_long,
    topic_name_not_terminated,
    ring_offset_before_registry_end,
    ring_offset_misaligned,
    ring_allocation_outside_segment,
    zero_total_slots,
    zero_slot_size,
    ring_size_mismatch,
};

/// \brief Fixed-layout header at the start of an EVerest SHM transport segment.
///
/// This structure is part of the cross-process SHM ABI. Keep it limited to fixed-width integer fields.
/// Ring buffers and the future topic registry are addressed by byte offsets from the segment base.
struct SegmentHeader {
    std::uint64_t magic;
    std::uint32_t layout_version;
    std::uint32_t header_size;
    std::uint64_t segment_size;
    std::uint64_t registry_offset;
    std::uint32_t registry_entry_count;
    std::uint32_t registry_entry_capacity;
    std::uint64_t registry_entry_size;
    std::uint64_t reserved[2];
};

static_assert(std::is_standard_layout<SegmentHeader>::value, "SegmentHeader must keep a stable standard layout");
static_assert(std::is_trivially_copyable<SegmentHeader>::value, "SegmentHeader must be trivially copyable");
static_assert(sizeof(SegmentHeader) == 64, "SegmentHeader layout size is part of the SHM ABI");
static_assert(alignof(SegmentHeader) <= shm_segment_registry_alignment,
              "SegmentHeader alignment must not exceed the registry alignment");

/// \brief Fixed-layout topic entry stored in the segment registry table.
///
/// Layout direction for multi-topic SHM segments:
/// one SegmentHeader at offset 0, followed by a fixed-capacity TopicRegistryEntry table at
/// SegmentHeader::registry_offset, followed by per-topic ring buffers in the same mapped segment. Entries identify
/// ring buffers by byte offsets from the segment base. Topic names are stored inline and writes reject names that do
/// not fit; names are never silently truncated.
struct TopicRegistryEntry {
    std::uint32_t flags;
    std::uint32_t topic_name_length;
    std::uint64_t ring_offset;
    std::uint64_t ring_size;
    std::uint32_t total_slots;
    std::uint32_t slot_size;
    std::uint64_t reserved[4];
    std::uint8_t topic_name[shm_topic_name_capacity];
};

static_assert(std::is_standard_layout<TopicRegistryEntry>::value,
              "TopicRegistryEntry must keep a stable standard layout");
static_assert(std::is_trivially_copyable<TopicRegistryEntry>::value, "TopicRegistryEntry must be trivially copyable");
static_assert(sizeof(TopicRegistryEntry) == shm_segment_registry_entry_size,
              "TopicRegistryEntry layout size is part of the SHM ABI");
static_assert(alignof(TopicRegistryEntry) <= shm_segment_registry_alignment,
              "TopicRegistryEntry alignment must not exceed the registry alignment");

/// \brief Return the next offset aligned for registry entries and topic ring buffers.
std::uint64_t align_shm_topic_offset(std::uint64_t offset);

/// \brief Validate a mapped EVerest SHM segment header before using offsets stored in it.
SegmentHeaderValidationStatus validate_segment_header(const void* segment_base, std::uint64_t mapped_segment_size);

/// \brief Initialize a mapped EVerest SHM segment header.
SegmentHeaderValidationStatus initialize_segment_header(void* segment_base, std::uint64_t mapped_segment_size,
                                                        std::uint64_t registry_offset,
                                                        std::uint32_t registry_entry_capacity,
                                                        std::uint32_t registry_entry_count = 0U);

/// \brief Maximum number of distinct subscribers tracked per topic.
///
/// Each subscriber owns one progress-table entry indexed by the coordinator-assigned
/// subscriber id. The table is fixed-size because it is part of every topic ring layout.
constexpr std::uint32_t shm_max_subscribers_per_topic = 32U;

/// \brief Per-subscriber monotonic dispatch / acked-slot counters visible across processes.
///
/// `acked_count` is updated by the subscriber side of each topic (cross-process atomic increment)
/// immediately before it signals the per-topic ACK eventfd. The coordinator reads each subscriber's
/// value lazily on every ACK signal and applies the delta to its in-memory pending_slots queue.
/// Storing this counter in shared memory removes the need for one ACK eventfd per (topic,
/// subscriber) pair.
///
/// `dispatched_count` is updated by the coordinator (cross-process atomic increment) every time it
/// commits a slot to that subscriber's pending queue. The subscriber side compares it against its
/// own local acked-count to decide how many slots to consume per wake-up. Storing this counter
/// in shared memory lets the runtime use one client-owned wake FD per persistent shm_client
/// session. The subscriber-side wake-up signal is only a nudge to re-scan SHM progress and
/// carries no slot-count semantics.
///
/// Each entry is aligned and padded to a full cache line. Without this padding up to four
/// subscribers would share the same 64-byte line, so the coordinator's writes to one
/// subscriber's dispatched_count and another subscriber's reads/writes to their own counters
/// would ping-pong the same line between cores under high-fan-out load. Putting every entry on
/// its own line eliminates cross-subscriber false sharing; the two counters inside one entry
/// still share a line, but those reflect the same publish/ACK loop for a single subscriber so
/// the bouncing is unavoidable without doubling the table footprint.
struct alignas(shm_cache_line_size) SubscriberAckProgress {
    std::atomic<std::uint64_t> acked_count;      ///< Total slots acknowledged by this subscriber.
    std::atomic<std::uint64_t> dispatched_count; ///< Total slots dispatched to this subscriber by the coordinator.
};

static_assert(std::is_standard_layout<SubscriberAckProgress>::value,
              "SubscriberAckProgress must keep a stable standard layout");
static_assert(sizeof(SubscriberAckProgress) == shm_cache_line_size,
              "SubscriberAckProgress is part of the SHM ABI and must remain one cache line");
static_assert(alignof(SubscriberAckProgress) == shm_cache_line_size,
              "SubscriberAckProgress must be cache-line aligned to avoid cross-subscriber false sharing");

/// \brief Table of per-subscriber ack progress entries laid out immediately after RingbufferMetadata.
struct alignas(shm_cache_line_size) SubscriberAckProgressTable {
    SubscriberAckProgress entries[shm_max_subscribers_per_topic];
};

static_assert(sizeof(SubscriberAckProgressTable) == shm_max_subscribers_per_topic * sizeof(SubscriberAckProgress),
              "SubscriberAckProgressTable size is part of the SHM ABI");
static_assert(alignof(SubscriberAckProgressTable) == shm_cache_line_size,
              "SubscriberAckProgressTable must remain cache-line aligned");

/// \brief Metadata for a single ringbuffer topic in shared memory
struct alignas(shm_cache_line_size) RingbufferMetadata {
    std::uint32_t total_slots;                     ///< Number of slots in the ringbuffer
    std::uint32_t slot_size;                       ///< Size of the payload area in each slot
    std::atomic<std::uint32_t> target_subscribers; ///< Current number of active subscribers
    std::atomic<std::uint32_t> write_idx;          ///< Current slot the publisher is targeting
    std::atomic<std::uint64_t> publication_count;  ///< Total publications committed by publishers.
    std::atomic<std::uint64_t> release_count;      ///< Total writer-blocking slots released by coordinator.
    std::atomic<std::uint64_t> next_sequence;      ///< Next per-topic publication sequence assigned to a publisher.
    std::atomic<std::uint32_t> publish_lock;       ///< Per-topic cross-process publisher serialization lock.
};

/// \brief Header for a single slot in the ringbuffer
struct ShmSlotHeader {
    std::uint64_t sequence;                        ///< Monotonic ID to identify data "age"
    std::uint32_t data_length;                     ///< Size of the actual JSON payload
    std::uint32_t flags;                           ///< Publication metadata such as retained-topic handling
    std::atomic<std::uint32_t> target_subscribers; ///< Subscriber target for this specific publication
    std::atomic<std::uint32_t> ack_count;          ///< Atomic counter of ACKs received

    /// \brief Cache-line alignment to prevent false sharing.
    /// Standard cache line size is 64 bytes on most modern architectures.
    char padding[64 - (sizeof(std::uint64_t) + (2 * sizeof(std::uint32_t)) + (2 * sizeof(std::atomic<std::uint32_t>)))];
};

static_assert(sizeof(ShmSlotHeader) == 64, "ShmSlotHeader must be exactly 64 bytes (one cache line)");
static_assert(sizeof(RingbufferMetadata) == shm_cache_line_size, "RingbufferMetadata must be exactly one cache line");
static_assert(alignof(RingbufferMetadata) == shm_cache_line_size, "RingbufferMetadata must be cache-line aligned");

std::uint64_t shm_align_to_cache_line(std::uint64_t value);

std::uint64_t shm_ring_buffer_slot_stride(std::uint32_t slot_size);

/// \brief Constant header overhead at the start of every topic ring buffer.
///
/// Composed of RingbufferMetadata (one cache line) followed by SubscriberAckProgressTable. The
/// progress table lets the coordinator route ACKs through a single per-topic eventfd while still
/// distinguishing subscriber identity.
inline constexpr std::uint64_t shm_ring_buffer_header_size() {
    return sizeof(RingbufferMetadata) + sizeof(SubscriberAckProgressTable);
}

bool shm_ring_buffer_required_size_overflows(std::uint32_t total_slots, std::uint32_t slot_size);

std::uint64_t shm_ring_buffer_required_size_u64(std::uint32_t total_slots, std::uint32_t slot_size);

std::size_t shm_ring_buffer_required_size(std::uint32_t total_slots, std::uint32_t slot_size);

std::uint64_t topic_registry_table_end_offset(const SegmentHeader& header);

std::uint64_t first_topic_ring_offset_after_registry(const SegmentHeader& header);

TopicRegistryEntry* topic_registry_entry_at(void* segment_base, const SegmentHeader& header, std::uint32_t index);

const TopicRegistryEntry* topic_registry_entry_at(const void* segment_base, const SegmentHeader& header,
                                                  std::uint32_t index);

TopicRegistryValidationStatus initialize_topic_registry_entry(TopicRegistryEntry* entry, std::string_view topic_name,
                                                              std::uint64_t ring_offset, std::uint32_t total_slots,
                                                              std::uint32_t slot_size);

TopicRegistryValidationStatus validate_topic_registry_entry(const void* segment_base, std::uint64_t mapped_segment_size,
                                                            const SegmentHeader* header,
                                                            const TopicRegistryEntry* entry);

const TopicRegistryEntry* find_topic_registry_entry(const void* segment_base, std::uint64_t mapped_segment_size,
                                                    const SegmentHeader* header, std::string_view topic_name);

} // namespace everest::lib::io::shm
