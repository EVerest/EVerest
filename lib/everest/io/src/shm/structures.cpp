// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/io/shm/structures.hpp>

#include <cstring>
#include <limits>

namespace everest::lib::io::shm {
namespace {

bool shm_add_overflows(std::uint64_t lhs, std::uint64_t rhs) {
    return lhs > std::numeric_limits<std::uint64_t>::max() - rhs;
}

bool shm_multiply_overflows(std::uint64_t lhs, std::uint64_t rhs) {
    return lhs != 0U && rhs > std::numeric_limits<std::uint64_t>::max() / lhs;
}

} // namespace

std::uint64_t align_shm_topic_offset(std::uint64_t offset) {
    const std::uint64_t remainder = offset % shm_topic_ring_alignment;
    if (remainder == 0U) {
        return offset;
    }
    if (shm_add_overflows(offset, shm_topic_ring_alignment - remainder)) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return offset + (shm_topic_ring_alignment - remainder);
}

SegmentHeaderValidationStatus validate_segment_header(const void* segment_base, std::uint64_t mapped_segment_size) {
    if (segment_base == nullptr) {
        return SegmentHeaderValidationStatus::null_segment;
    }
    if (mapped_segment_size < sizeof(SegmentHeader)) {
        return SegmentHeaderValidationStatus::mapped_segment_too_small;
    }

    const auto* header = static_cast<const SegmentHeader*>(segment_base);
    if (header->magic != shm_segment_magic) {
        return SegmentHeaderValidationStatus::bad_magic;
    }
    if (header->layout_version != shm_segment_layout_version || header->header_size != sizeof(SegmentHeader) ||
        header->registry_entry_size != shm_segment_registry_entry_size) {
        return SegmentHeaderValidationStatus::incompatible_version;
    }
    if (header->segment_size < sizeof(SegmentHeader)) {
        return SegmentHeaderValidationStatus::segment_size_smaller_than_header;
    }
    if (header->segment_size > mapped_segment_size) {
        return SegmentHeaderValidationStatus::segment_size_larger_than_mapping;
    }
    if (header->registry_offset < sizeof(SegmentHeader)) {
        return SegmentHeaderValidationStatus::registry_offset_before_header;
    }
    if (header->registry_offset >= header->segment_size) {
        return SegmentHeaderValidationStatus::registry_offset_outside_segment;
    }
    if ((header->registry_offset % shm_segment_registry_alignment) != 0U) {
        return SegmentHeaderValidationStatus::registry_offset_misaligned;
    }
    if (header->registry_entry_count > header->registry_entry_capacity) {
        return SegmentHeaderValidationStatus::registry_entry_count_exceeds_capacity;
    }
    if (shm_multiply_overflows(header->registry_entry_capacity, header->registry_entry_size)) {
        return SegmentHeaderValidationStatus::registry_metadata_outside_segment;
    }

    const std::uint64_t registry_bytes = header->registry_entry_capacity * header->registry_entry_size;
    if (shm_add_overflows(header->registry_offset, registry_bytes) ||
        header->registry_offset + registry_bytes > header->segment_size) {
        return SegmentHeaderValidationStatus::registry_metadata_outside_segment;
    }

    return SegmentHeaderValidationStatus::valid;
}

SegmentHeaderValidationStatus initialize_segment_header(void* segment_base, std::uint64_t mapped_segment_size,
                                                        std::uint64_t registry_offset,
                                                        std::uint32_t registry_entry_capacity,
                                                        std::uint32_t registry_entry_count) {
    if (segment_base == nullptr) {
        return SegmentHeaderValidationStatus::null_segment;
    }
    if (mapped_segment_size < sizeof(SegmentHeader)) {
        return SegmentHeaderValidationStatus::mapped_segment_too_small;
    }

    auto* header = static_cast<SegmentHeader*>(segment_base);
    std::memset(header, 0, sizeof(SegmentHeader));
    header->magic = shm_segment_magic;
    header->layout_version = shm_segment_layout_version;
    header->header_size = sizeof(SegmentHeader);
    header->segment_size = mapped_segment_size;
    header->registry_offset = registry_offset;
    header->registry_entry_count = registry_entry_count;
    header->registry_entry_capacity = registry_entry_capacity;
    header->registry_entry_size = shm_segment_registry_entry_size;

    return validate_segment_header(segment_base, mapped_segment_size);
}

std::uint64_t shm_align_to_cache_line(std::uint64_t value) {
    const std::uint64_t remainder = value % shm_cache_line_size;
    if (remainder == 0U) {
        return value;
    }
    if (shm_add_overflows(value, shm_cache_line_size - remainder)) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return value + (shm_cache_line_size - remainder);
}

std::uint64_t shm_ring_buffer_slot_stride(std::uint32_t slot_size) {
    const auto raw_slot_size =
        static_cast<std::uint64_t>(sizeof(ShmSlotHeader)) + static_cast<std::uint64_t>(slot_size);
    return shm_align_to_cache_line(raw_slot_size);
}

bool shm_ring_buffer_required_size_overflows(std::uint32_t total_slots, std::uint32_t slot_size) {
    const auto slot_stride = shm_ring_buffer_slot_stride(slot_size);
    return slot_stride == std::numeric_limits<std::uint64_t>::max() ||
           shm_multiply_overflows(total_slots, slot_stride) ||
           shm_add_overflows(shm_ring_buffer_header_size(), static_cast<std::uint64_t>(total_slots) * slot_stride);
}

std::uint64_t shm_ring_buffer_required_size_u64(std::uint32_t total_slots, std::uint32_t slot_size) {
    const auto slot_stride = shm_ring_buffer_slot_stride(slot_size);
    if (shm_ring_buffer_required_size_overflows(total_slots, slot_size)) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return shm_ring_buffer_header_size() + (static_cast<std::uint64_t>(total_slots) * slot_stride);
}

std::size_t shm_ring_buffer_required_size(std::uint32_t total_slots, std::uint32_t slot_size) {
    return static_cast<std::size_t>(shm_ring_buffer_required_size_u64(total_slots, slot_size));
}

std::uint64_t topic_registry_table_end_offset(const SegmentHeader& header) {
    return header.registry_offset + (static_cast<std::uint64_t>(header.registry_entry_capacity) *
                                     static_cast<std::uint64_t>(header.registry_entry_size));
}

std::uint64_t first_topic_ring_offset_after_registry(const SegmentHeader& header) {
    return align_shm_topic_offset(topic_registry_table_end_offset(header));
}

TopicRegistryEntry* topic_registry_entry_at(void* segment_base, const SegmentHeader& header, std::uint32_t index) {
    if (segment_base == nullptr || index >= header.registry_entry_capacity) {
        return nullptr;
    }
    auto* bytes = static_cast<std::uint8_t*>(segment_base);
    return reinterpret_cast<TopicRegistryEntry*>(bytes + header.registry_offset +
                                                 (static_cast<std::uint64_t>(index) * header.registry_entry_size));
}

const TopicRegistryEntry* topic_registry_entry_at(const void* segment_base, const SegmentHeader& header,
                                                  std::uint32_t index) {
    if (segment_base == nullptr || index >= header.registry_entry_capacity) {
        return nullptr;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(segment_base);
    return reinterpret_cast<const TopicRegistryEntry*>(
        bytes + header.registry_offset + (static_cast<std::uint64_t>(index) * header.registry_entry_size));
}

TopicRegistryValidationStatus initialize_topic_registry_entry(TopicRegistryEntry* entry, std::string_view topic_name,
                                                              std::uint64_t ring_offset, std::uint32_t total_slots,
                                                              std::uint32_t slot_size) {
    if (entry == nullptr) {
        return TopicRegistryValidationStatus::null_entry;
    }
    if (topic_name.empty()) {
        return TopicRegistryValidationStatus::empty_topic_name;
    }
    if (topic_name.size() > shm_topic_name_capacity) {
        return TopicRegistryValidationStatus::topic_name_too_long;
    }
    if (total_slots == 0U) {
        return TopicRegistryValidationStatus::zero_total_slots;
    }
    if (slot_size == 0U) {
        return TopicRegistryValidationStatus::zero_slot_size;
    }
    if (shm_ring_buffer_required_size_overflows(total_slots, slot_size)) {
        return TopicRegistryValidationStatus::ring_size_mismatch;
    }

    std::memset(entry, 0, sizeof(TopicRegistryEntry));
    entry->flags = shm_topic_registry_entry_active;
    entry->topic_name_length = static_cast<std::uint32_t>(topic_name.size());
    entry->ring_offset = ring_offset;
    entry->ring_size = shm_ring_buffer_required_size_u64(total_slots, slot_size);
    entry->total_slots = total_slots;
    entry->slot_size = slot_size;
    std::memcpy(entry->topic_name, topic_name.data(), topic_name.size());

    return TopicRegistryValidationStatus::valid;
}

TopicRegistryValidationStatus validate_topic_registry_entry(const void* segment_base, std::uint64_t mapped_segment_size,
                                                            const SegmentHeader* header,
                                                            const TopicRegistryEntry* entry) {
    if (segment_base == nullptr) {
        return TopicRegistryValidationStatus::null_segment;
    }
    if (header == nullptr) {
        return TopicRegistryValidationStatus::null_header;
    }
    if (entry == nullptr) {
        return TopicRegistryValidationStatus::null_entry;
    }
    if (validate_segment_header(segment_base, mapped_segment_size) != SegmentHeaderValidationStatus::valid) {
        return TopicRegistryValidationStatus::invalid_segment_header;
    }
    if (header != static_cast<const SegmentHeader*>(segment_base)) {
        return TopicRegistryValidationStatus::invalid_segment_header;
    }
    if ((entry->flags & shm_topic_registry_entry_active) == 0U) {
        return TopicRegistryValidationStatus::free_entry;
    }
    if (entry->topic_name_length == 0U) {
        return TopicRegistryValidationStatus::empty_topic_name;
    }
    if (entry->topic_name_length > shm_topic_name_capacity) {
        return TopicRegistryValidationStatus::topic_name_too_long;
    }
    if (entry->topic_name_length < shm_topic_name_capacity && entry->topic_name[entry->topic_name_length] != 0U) {
        return TopicRegistryValidationStatus::topic_name_not_terminated;
    }
    if (entry->ring_offset < first_topic_ring_offset_after_registry(*header)) {
        return TopicRegistryValidationStatus::ring_offset_before_registry_end;
    }
    if ((entry->ring_offset % shm_topic_ring_alignment) != 0U) {
        return TopicRegistryValidationStatus::ring_offset_misaligned;
    }
    if (entry->total_slots == 0U) {
        return TopicRegistryValidationStatus::zero_total_slots;
    }
    if (entry->slot_size == 0U) {
        return TopicRegistryValidationStatus::zero_slot_size;
    }

    if (shm_ring_buffer_required_size_overflows(entry->total_slots, entry->slot_size)) {
        return TopicRegistryValidationStatus::ring_size_mismatch;
    }

    const std::uint64_t expected_ring_size = shm_ring_buffer_required_size_u64(entry->total_slots, entry->slot_size);
    if (entry->ring_size != expected_ring_size) {
        return TopicRegistryValidationStatus::ring_size_mismatch;
    }
    if (shm_add_overflows(entry->ring_offset, entry->ring_size) ||
        entry->ring_offset + entry->ring_size > header->segment_size ||
        entry->ring_offset + entry->ring_size > mapped_segment_size) {
        return TopicRegistryValidationStatus::ring_allocation_outside_segment;
    }

    return TopicRegistryValidationStatus::valid;
}

const TopicRegistryEntry* find_topic_registry_entry(const void* segment_base, std::uint64_t mapped_segment_size,
                                                    const SegmentHeader* header, std::string_view topic_name) {
    if (segment_base == nullptr || header == nullptr || topic_name.empty() ||
        topic_name.size() > shm_topic_name_capacity ||
        validate_segment_header(segment_base, mapped_segment_size) != SegmentHeaderValidationStatus::valid) {
        return nullptr;
    }

    for (std::uint32_t index = 0U; index < header->registry_entry_count; ++index) {
        const auto* entry = topic_registry_entry_at(segment_base, *header, index);
        if (validate_topic_registry_entry(segment_base, mapped_segment_size, header, entry) !=
            TopicRegistryValidationStatus::valid) {
            continue;
        }
        if (entry->topic_name_length == topic_name.size() &&
            std::memcmp(entry->topic_name, topic_name.data(), topic_name.size()) == 0) {
            return entry;
        }
    }

    return nullptr;
}

} // namespace everest::lib::io::shm
