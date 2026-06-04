// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>

#include <everest/io/shm/ring_buffer.hpp>
#include <everest/io/shm/structures.hpp>

using everest::lib::io::shm::find_topic_registry_entry;
using everest::lib::io::shm::first_topic_ring_offset_after_registry;
using everest::lib::io::shm::initialize_segment_header;
using everest::lib::io::shm::initialize_topic_registry_entry;
using everest::lib::io::shm::ring_buffer;
using everest::lib::io::shm::RingbufferMetadata;
using everest::lib::io::shm::SegmentHeader;
using everest::lib::io::shm::shm_cache_line_size;
using everest::lib::io::shm::shm_ring_buffer_slot_stride;
using everest::lib::io::shm::shm_segment_registry_entry_size;
using everest::lib::io::shm::shm_topic_name_capacity;
using everest::lib::io::shm::shm_topic_registry_entry_active;
using everest::lib::io::shm::topic_registry_entry_at;
using everest::lib::io::shm::topic_registry_table_end_offset;
using everest::lib::io::shm::TopicRegistryEntry;
using everest::lib::io::shm::TopicRegistryValidationStatus;
using everest::lib::io::shm::validate_topic_registry_entry;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

void require_status(TopicRegistryValidationStatus actual, TopicRegistryValidationStatus expected, const char* message) {
    if (actual != expected) {
        std::cerr << "FAILED: " << message << " (actual=" << static_cast<std::uint32_t>(actual)
                  << ", expected=" << static_cast<std::uint32_t>(expected) << ")\n";
        std::exit(1);
    }
}

struct TestSegment {
    static constexpr std::uint64_t segment_size = 16384U;
    static constexpr std::uint64_t registry_offset = sizeof(SegmentHeader);
    static constexpr std::uint32_t registry_capacity = 4U;

    alignas(RingbufferMetadata) std::array<std::uint8_t, segment_size> bytes{};

    TestSegment() {
        auto status = initialize_segment_header(bytes.data(), bytes.size(), registry_offset, registry_capacity);
        require(static_cast<std::uint32_t>(status) == 0U, "valid segment header initialization should succeed");
    }

    SegmentHeader* header() {
        return reinterpret_cast<SegmentHeader*>(bytes.data());
    }

    const SegmentHeader* header() const {
        return reinterpret_cast<const SegmentHeader*>(bytes.data());
    }

    TopicRegistryEntry* entry(std::uint32_t index) {
        return topic_registry_entry_at(bytes.data(), *header(), index);
    }
};

void test_fixed_layout_properties() {
    static_assert(std::is_standard_layout<TopicRegistryEntry>::value, "TopicRegistryEntry must be standard layout");
    static_assert(std::is_trivially_copyable<TopicRegistryEntry>::value,
                  "TopicRegistryEntry must be trivially copyable");
    static_assert(sizeof(TopicRegistryEntry) == shm_segment_registry_entry_size,
                  "TopicRegistryEntry must stay within the segment header entry size");

    require(offsetof(TopicRegistryEntry, flags) == 0U, "flags offset should be stable");
    require(offsetof(TopicRegistryEntry, topic_name_length) == 4U, "topic name length offset should be stable");
    require(offsetof(TopicRegistryEntry, ring_offset) == 8U, "ring offset should be stable");
    require(offsetof(TopicRegistryEntry, ring_size) == 16U, "ring size should be stable");
    require(offsetof(TopicRegistryEntry, total_slots) == 24U, "total slots offset should be stable");
    require(offsetof(TopicRegistryEntry, slot_size) == 28U, "slot size offset should be stable");
    require(offsetof(TopicRegistryEntry, reserved) == 32U, "reserved offset should be stable");
    require(offsetof(TopicRegistryEntry, topic_name) == 64U, "topic name offset should be stable");
    require(sizeof(RingbufferMetadata) == shm_cache_line_size, "ring metadata should occupy one cache line");
    require(alignof(RingbufferMetadata) == shm_cache_line_size, "ring metadata should be cache-line aligned");
    require(shm_ring_buffer_slot_stride(1U) == 128U, "ring slots should have cache-line rounded stride");
}

void test_valid_segment_registry_table_and_one_topic_ring() {
    TestSegment segment;
    segment.header()->registry_entry_count = 1U;

    constexpr std::uint32_t slots = 3U;
    constexpr std::uint32_t slot_size = 128U;
    const auto ring_offset = first_topic_ring_offset_after_registry(*segment.header());
    auto* entry = segment.entry(0U);

    require_status(initialize_topic_registry_entry(entry, "evse/limits", ring_offset, slots, slot_size),
                   TopicRegistryValidationStatus::valid, "valid topic entry initialization should succeed");
    require_status(validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), entry),
                   TopicRegistryValidationStatus::valid, "valid topic entry should validate");
    require(entry->ring_size == ring_buffer::calculate_required_size(slots, slot_size),
            "entry ring size should match ring_buffer calculation");

    ring_buffer rb(segment.bytes.data() + entry->ring_offset);
    rb.get_metadata()->total_slots = slots;
    rb.get_metadata()->slot_size = slot_size;
    require(rb.get_metadata()->total_slots == entry->total_slots, "ring metadata should be reachable at entry offset");
}

void test_two_registry_entries_have_distinct_ring_offsets() {
    TestSegment segment;
    segment.header()->registry_entry_count = 2U;

    const auto ring_a_offset = first_topic_ring_offset_after_registry(*segment.header());
    const auto ring_a_size = ring_buffer::calculate_required_size(2U, 64U);
    const auto ring_b_offset = everest::lib::io::shm::align_shm_topic_offset(ring_a_offset + ring_a_size);

    require_status(initialize_topic_registry_entry(segment.entry(0U), "topic/a", ring_a_offset, 2U, 64U),
                   TopicRegistryValidationStatus::valid, "first topic should initialize");
    require_status(initialize_topic_registry_entry(segment.entry(1U), "topic/b", ring_b_offset, 4U, 32U),
                   TopicRegistryValidationStatus::valid, "second topic should initialize");
    require(segment.entry(0U)->ring_offset != segment.entry(1U)->ring_offset, "topic ring offsets should be distinct");
    require_status(
        validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), segment.entry(0U)),
        TopicRegistryValidationStatus::valid, "first topic should validate");
    require_status(
        validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), segment.entry(1U)),
        TopicRegistryValidationStatus::valid, "second topic should validate");
}

void test_find_topics_by_name() {
    TestSegment segment;
    segment.header()->registry_entry_count = 2U;

    const auto ring_a_offset = first_topic_ring_offset_after_registry(*segment.header());
    const auto ring_b_offset =
        everest::lib::io::shm::align_shm_topic_offset(ring_a_offset + ring_buffer::calculate_required_size(2U, 64U));
    initialize_topic_registry_entry(segment.entry(0U), "telemetry/current", ring_a_offset, 2U, 64U);
    initialize_topic_registry_entry(segment.entry(1U), "telemetry/voltage", ring_b_offset, 2U, 64U);

    const auto* found =
        find_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), "telemetry/voltage");
    require(found == segment.entry(1U), "lookup should return the matching active topic entry");
}

void test_missing_topic_lookup_returns_null() {
    TestSegment segment;
    segment.header()->registry_entry_count = 1U;
    initialize_topic_registry_entry(segment.entry(0U), "telemetry/current",
                                    first_topic_ring_offset_after_registry(*segment.header()), 2U, 64U);

    require(find_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(),
                                      "telemetry/missing") == nullptr,
            "missing topic lookup should return null");
}

void test_rejects_null_inputs() {
    TestSegment segment;
    initialize_topic_registry_entry(segment.entry(0U), "topic",
                                    first_topic_ring_offset_after_registry(*segment.header()), 2U, 64U);

    require_status(validate_topic_registry_entry(nullptr, segment.bytes.size(), segment.header(), segment.entry(0U)),
                   TopicRegistryValidationStatus::null_segment, "null segment should be rejected");
    require_status(
        validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), nullptr, segment.entry(0U)),
        TopicRegistryValidationStatus::null_header, "null header should be rejected");
    require_status(validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), nullptr),
                   TopicRegistryValidationStatus::null_entry, "null entry should be rejected");
}

void test_rejects_invalid_segment_header() {
    TestSegment segment;
    initialize_topic_registry_entry(segment.entry(0U), "topic",
                                    first_topic_ring_offset_after_registry(*segment.header()), 2U, 64U);
    segment.header()->magic = 0U;

    require_status(
        validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), segment.entry(0U)),
        TopicRegistryValidationStatus::invalid_segment_header,
        "topic validation should reject an invalid segment header");
}

void test_rejects_free_entry() {
    TestSegment segment;
    require_status(
        validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), segment.entry(0U)),
        TopicRegistryValidationStatus::free_entry, "free entry should not validate as active");
}

void test_rejects_topic_name_errors() {
    TestSegment segment;
    auto* entry = segment.entry(0U);

    require_status(
        initialize_topic_registry_entry(entry, "", first_topic_ring_offset_after_registry(*segment.header()), 2U, 64U),
        TopicRegistryValidationStatus::empty_topic_name, "empty topic names should be rejected");

    std::string too_long(shm_topic_name_capacity + 1U, 'x');
    require_status(initialize_topic_registry_entry(entry, too_long,
                                                   first_topic_ring_offset_after_registry(*segment.header()), 2U, 64U),
                   TopicRegistryValidationStatus::topic_name_too_long, "too-long topic names should be rejected");

    initialize_topic_registry_entry(entry, "topic", first_topic_ring_offset_after_registry(*segment.header()), 2U, 64U);
    entry->topic_name[entry->topic_name_length] = 'x';
    require_status(validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), entry),
                   TopicRegistryValidationStatus::topic_name_not_terminated,
                   "inline names shorter than capacity should retain a terminator byte");
}

void test_rejects_bad_ring_offsets_and_bounds() {
    TestSegment segment;
    auto* entry = segment.entry(0U);
    const auto first_ring_offset = first_topic_ring_offset_after_registry(*segment.header());

    initialize_topic_registry_entry(entry, "topic", first_ring_offset - 8U, 2U, 64U);
    require_status(validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), entry),
                   TopicRegistryValidationStatus::ring_offset_before_registry_end,
                   "ring offsets before the registry table end should be rejected");

    initialize_topic_registry_entry(entry, "topic", first_ring_offset + 1U, 2U, 64U);
    require_status(validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), entry),
                   TopicRegistryValidationStatus::ring_offset_misaligned, "misaligned ring offsets should be rejected");

    initialize_topic_registry_entry(entry, "topic", segment.header()->segment_size - 64U, 2U, 64U);
    require_status(validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), entry),
                   TopicRegistryValidationStatus::ring_allocation_outside_segment,
                   "ring allocations extending past the segment should be rejected");
}

void test_rejects_bad_ring_metadata() {
    TestSegment segment;
    auto* entry = segment.entry(0U);

    require_status(initialize_topic_registry_entry(entry, "topic",
                                                   first_topic_ring_offset_after_registry(*segment.header()), 0U, 64U),
                   TopicRegistryValidationStatus::zero_total_slots, "zero slots should be rejected on write");
    require_status(initialize_topic_registry_entry(entry, "topic",
                                                   first_topic_ring_offset_after_registry(*segment.header()), 2U, 0U),
                   TopicRegistryValidationStatus::zero_slot_size, "zero slot size should be rejected on write");

    initialize_topic_registry_entry(entry, "topic", first_topic_ring_offset_after_registry(*segment.header()), 2U, 64U);
    entry->total_slots = 0U;
    require_status(validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), entry),
                   TopicRegistryValidationStatus::zero_total_slots, "zero slots should be rejected on validate");

    initialize_topic_registry_entry(entry, "topic", first_topic_ring_offset_after_registry(*segment.header()), 2U, 64U);
    entry->slot_size = 0U;
    require_status(validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), entry),
                   TopicRegistryValidationStatus::zero_slot_size, "zero slot size should be rejected on validate");

    initialize_topic_registry_entry(entry, "topic", first_topic_ring_offset_after_registry(*segment.header()), 2U, 64U);
    entry->ring_size += 1U;
    require_status(validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), entry),
                   TopicRegistryValidationStatus::ring_size_mismatch,
                   "ring size inconsistent with ring_buffer calculation should be rejected");

    initialize_topic_registry_entry(entry, "topic", first_topic_ring_offset_after_registry(*segment.header()), 2U, 64U);
    entry->slot_size = std::numeric_limits<std::uint32_t>::max() - 32U;
    entry->ring_size = 64U;
    require_status(validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), entry),
                   TopicRegistryValidationStatus::ring_size_mismatch,
                   "slot size addition overflow should be rejected before ring size validation");
}

void test_accepts_long_framework_topic_names() {
    static_assert(shm_topic_name_capacity >= 256U,
                  "SHM topic name capacity must accommodate generated framework error topics");

    const std::string long_topic =
        "everest/modules/cb_bsp/impl/connector_lock/error/connector_lock/ConnectorLockCapNotCharged";
    require(long_topic.size() > 64U, "the regression topic must exceed the previous 64-byte capacity");
    require(long_topic.size() <= shm_topic_name_capacity, "the regression topic must fit the updated capacity");

    TestSegment segment;
    segment.header()->registry_entry_count = 1U;
    auto* entry = segment.entry(0U);
    require_status(initialize_topic_registry_entry(entry, long_topic,
                                                   first_topic_ring_offset_after_registry(*segment.header()), 2U, 64U),
                   TopicRegistryValidationStatus::valid,
                   "long framework topic names should fit the registry entry capacity");
    require_status(validate_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), entry),
                   TopicRegistryValidationStatus::valid, "long framework topic entry should validate");
    require(entry->topic_name_length == long_topic.size(),
            "registry entry should record the full long topic name length");

    const auto* found =
        find_topic_registry_entry(segment.bytes.data(), segment.bytes.size(), segment.header(), long_topic);
    require(found == entry, "lookup should find the long framework topic by name");
}

void test_next_ring_offset_after_registry() {
    TestSegment segment;
    require(first_topic_ring_offset_after_registry(*segment.header()) >=
                topic_registry_table_end_offset(*segment.header()),
            "first ring offset should be after the registry table");
    require((first_topic_ring_offset_after_registry(*segment.header()) %
             everest::lib::io::shm::shm_topic_ring_alignment) == 0U,
            "first ring offset should be aligned");
}

int main() {
    test_fixed_layout_properties();
    test_valid_segment_registry_table_and_one_topic_ring();
    test_two_registry_entries_have_distinct_ring_offsets();
    test_find_topics_by_name();
    test_missing_topic_lookup_returns_null();
    test_rejects_null_inputs();
    test_rejects_invalid_segment_header();
    test_rejects_free_entry();
    test_rejects_topic_name_errors();
    test_rejects_bad_ring_offsets_and_bounds();
    test_rejects_bad_ring_metadata();
    test_accepts_long_framework_topic_names();
    test_next_ring_offset_after_registry();

    return 0;
}
