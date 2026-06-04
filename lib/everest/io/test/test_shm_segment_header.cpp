// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include <everest/io/shm/structures.hpp>

using everest::lib::io::shm::initialize_segment_header;
using everest::lib::io::shm::SegmentHeader;
using everest::lib::io::shm::SegmentHeaderValidationStatus;
using everest::lib::io::shm::shm_segment_layout_version;
using everest::lib::io::shm::shm_segment_magic;
using everest::lib::io::shm::shm_segment_registry_entry_size;
using everest::lib::io::shm::validate_segment_header;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

void require_status(SegmentHeaderValidationStatus actual, SegmentHeaderValidationStatus expected, const char* message) {
    if (actual != expected) {
        std::cerr << "FAILED: " << message << " (actual=" << static_cast<std::uint32_t>(actual)
                  << ", expected=" << static_cast<std::uint32_t>(expected) << ")\n";
        std::exit(1);
    }
}

struct TestSegment {
    static constexpr std::uint64_t registry_offset = sizeof(SegmentHeader);
    static constexpr std::uint32_t registry_capacity = 4U;
    static constexpr std::uint64_t segment_size =
        registry_offset + (registry_capacity * shm_segment_registry_entry_size) + 1024U;

    alignas(SegmentHeader) std::array<std::uint8_t, segment_size> bytes{};

    TestSegment() {
        require_status(initialize_segment_header(bytes.data(), bytes.size(), registry_offset, registry_capacity),
                       SegmentHeaderValidationStatus::valid, "valid segment header initialization should succeed");
    }

    SegmentHeader* header() {
        return reinterpret_cast<SegmentHeader*>(bytes.data());
    }
};

void test_fixed_layout_properties() {
    static_assert(std::is_standard_layout<SegmentHeader>::value, "SegmentHeader must be standard layout");
    static_assert(std::is_trivially_copyable<SegmentHeader>::value, "SegmentHeader must be trivially copyable");
    static_assert(sizeof(SegmentHeader) == 64U, "SegmentHeader must remain fixed at 64 bytes");

    require(offsetof(SegmentHeader, magic) == 0U, "magic offset should be stable");
    require(offsetof(SegmentHeader, layout_version) == 8U, "layout version offset should be stable");
    require(offsetof(SegmentHeader, header_size) == 12U, "header size offset should be stable");
    require(offsetof(SegmentHeader, segment_size) == 16U, "segment size offset should be stable");
    require(offsetof(SegmentHeader, registry_offset) == 24U, "registry offset should be stable");
    require(offsetof(SegmentHeader, registry_entry_count) == 32U, "registry entry count offset should be stable");
    require(offsetof(SegmentHeader, registry_entry_capacity) == 36U, "registry entry capacity offset should be stable");
    require(offsetof(SegmentHeader, registry_entry_size) == 40U, "registry entry size offset should be stable");
    require(offsetof(SegmentHeader, reserved) == 48U, "reserved fields offset should be stable");
}

void test_valid_initialization() {
    TestSegment segment;
    const auto* header = segment.header();

    require(header->magic == shm_segment_magic, "initializer should write SHM magic");
    require(header->layout_version == shm_segment_layout_version, "initializer should write current layout version");
    require(header->header_size == sizeof(SegmentHeader), "initializer should write header size");
    require(header->segment_size == TestSegment::segment_size, "initializer should write total segment size");
    require(header->registry_offset == TestSegment::registry_offset, "initializer should write registry offset");
    require(header->registry_entry_count == 0U, "initializer should default registry entry count to zero");
    require(header->registry_entry_capacity == TestSegment::registry_capacity,
            "initializer should write registry entry capacity");
    require(header->registry_entry_size == shm_segment_registry_entry_size, "initializer should write entry size");
    require_status(validate_segment_header(segment.bytes.data(), segment.bytes.size()),
                   SegmentHeaderValidationStatus::valid, "valid initialized segment should validate");
}

void test_rejects_null_segment_for_validation() {
    require_status(validate_segment_header(nullptr, sizeof(SegmentHeader)), SegmentHeaderValidationStatus::null_segment,
                   "null segment should be rejected during validation");
}

void test_rejects_mapped_segment_smaller_than_header_for_validation() {
    alignas(SegmentHeader) std::array<std::uint8_t, sizeof(SegmentHeader)> bytes{};
    require_status(validate_segment_header(bytes.data(), sizeof(SegmentHeader) - 1U),
                   SegmentHeaderValidationStatus::mapped_segment_too_small,
                   "mapped segment smaller than header should be rejected during validation");
}

void test_rejects_segment_size_larger_than_mapping() {
    TestSegment segment;
    require_status(validate_segment_header(segment.bytes.data(), segment.bytes.size() - 1U),
                   SegmentHeaderValidationStatus::segment_size_larger_than_mapping,
                   "segment size larger than mapped size should be rejected");
}

void test_rejects_null_segment_for_initialization() {
    require_status(initialize_segment_header(nullptr, TestSegment::segment_size, TestSegment::registry_offset,
                                             TestSegment::registry_capacity),
                   SegmentHeaderValidationStatus::null_segment,
                   "null segment should be rejected during initialization");
}

void test_rejects_mapped_segment_smaller_than_header_for_initialization() {
    alignas(SegmentHeader) std::array<std::uint8_t, sizeof(SegmentHeader)> bytes{};
    require_status(initialize_segment_header(bytes.data(), sizeof(SegmentHeader) - 1U, TestSegment::registry_offset,
                                             TestSegment::registry_capacity),
                   SegmentHeaderValidationStatus::mapped_segment_too_small,
                   "mapped segment smaller than header should be rejected during initialization");
}

void test_rejects_bad_magic() {
    TestSegment segment;
    segment.header()->magic = 0U;
    require_status(validate_segment_header(segment.bytes.data(), segment.bytes.size()),
                   SegmentHeaderValidationStatus::bad_magic, "bad magic should be rejected");
}

void test_rejects_incompatible_version() {
    TestSegment segment;
    segment.header()->layout_version = shm_segment_layout_version + 1U;
    require_status(validate_segment_header(segment.bytes.data(), segment.bytes.size()),
                   SegmentHeaderValidationStatus::incompatible_version, "incompatible version should be rejected");
}

void test_rejects_segment_smaller_than_header() {
    TestSegment segment;
    segment.header()->segment_size = sizeof(SegmentHeader) - 1U;
    require_status(validate_segment_header(segment.bytes.data(), segment.bytes.size()),
                   SegmentHeaderValidationStatus::segment_size_smaller_than_header,
                   "segment size smaller than header should be rejected");
}

void test_rejects_registry_offset_before_header() {
    TestSegment segment;
    segment.header()->registry_offset = sizeof(SegmentHeader) - 1U;
    require_status(validate_segment_header(segment.bytes.data(), segment.bytes.size()),
                   SegmentHeaderValidationStatus::registry_offset_before_header,
                   "registry offset before header should be rejected");
}

void test_rejects_registry_offset_outside_segment() {
    TestSegment segment;
    segment.header()->registry_offset = segment.header()->segment_size;
    require_status(validate_segment_header(segment.bytes.data(), segment.bytes.size()),
                   SegmentHeaderValidationStatus::registry_offset_outside_segment,
                   "registry offset outside segment should be rejected");
}

void test_rejects_registry_metadata_that_cannot_fit() {
    TestSegment segment;
    segment.header()->registry_offset = TestSegment::segment_size - shm_segment_registry_entry_size + 8U;
    segment.header()->registry_entry_capacity = 1U;
    require_status(validate_segment_header(segment.bytes.data(), segment.bytes.size()),
                   SegmentHeaderValidationStatus::registry_metadata_outside_segment,
                   "registry metadata extending past segment should be rejected");
}

void test_rejects_misaligned_registry_offset() {
    TestSegment segment;
    segment.header()->registry_offset = sizeof(SegmentHeader) + 1U;
    require_status(validate_segment_header(segment.bytes.data(), segment.bytes.size()),
                   SegmentHeaderValidationStatus::registry_offset_misaligned,
                   "misaligned registry offset should be rejected");
}

void test_rejects_entry_count_above_capacity() {
    TestSegment segment;
    segment.header()->registry_entry_count = segment.header()->registry_entry_capacity + 1U;
    require_status(validate_segment_header(segment.bytes.data(), segment.bytes.size()),
                   SegmentHeaderValidationStatus::registry_entry_count_exceeds_capacity,
                   "registry entry count above capacity should be rejected");
}

int main() {
    test_fixed_layout_properties();
    test_valid_initialization();
    test_rejects_null_segment_for_validation();
    test_rejects_mapped_segment_smaller_than_header_for_validation();
    test_rejects_segment_size_larger_than_mapping();
    test_rejects_null_segment_for_initialization();
    test_rejects_mapped_segment_smaller_than_header_for_initialization();
    test_rejects_bad_magic();
    test_rejects_incompatible_version();
    test_rejects_segment_smaller_than_header();
    test_rejects_registry_offset_before_header();
    test_rejects_registry_offset_outside_segment();
    test_rejects_registry_metadata_that_cannot_fit();
    test_rejects_misaligned_registry_offset();
    test_rejects_entry_count_above_capacity();

    return 0;
}
