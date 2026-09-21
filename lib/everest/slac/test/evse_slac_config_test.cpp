// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include <everest/slac/EvseSlacConfig.hpp>

using namespace everest::lib::slac;
using namespace everest::lib::slac::fsm::evse;

namespace {

using Nmk = std::array<std::uint8_t, defs::NMK_LEN>;

bool assert_true(bool cond, const char* test_name, const char* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

bool is_legacy_base36_byte(std::uint8_t value) {
    return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z');
}

bool contains_non_legacy_base36_byte(Nmk const& nmk) {
    return std::any_of(nmk.begin(), nmk.end(), [](auto value) { return not is_legacy_base36_byte(value); });
}

bool contains_only_legacy_base36_bytes(Nmk const& nmk) {
    return std::all_of(nmk.begin(), nmk.end(), is_legacy_base36_byte);
}

bool test_generate_nmk_updates_session_nmk() {
    const char* test_name = "test_generate_nmk_updates_session_nmk";
    EvseSlacConfig config{};
    std::fill(std::begin(config.session_nmk), std::end(config.session_nmk), 0xA5);

    config.generate_nmk();

    return assert_true(not std::all_of(std::begin(config.session_nmk), std::end(config.session_nmk),
                                       [](auto value) { return value == 0xA5; }),
                       test_name, "generate_nmk() left session_nmk unchanged");
}

bool test_generate_nmk_writes_target_only() {
    const char* test_name = "test_generate_nmk_writes_target_only";
    EvseSlacConfig config{};
    std::fill(std::begin(config.session_nmk), std::end(config.session_nmk), 0xC3);
    std::array<std::uint8_t, defs::NMK_LEN + 2> buffer{};
    buffer.front() = 0xA5;
    buffer.back() = 0x5A;
    std::fill(buffer.begin() + 1, buffer.end() - 1, 0x11);

    config.generate_nmk(buffer.data() + 1);

    const auto target_changed =
        not std::all_of(buffer.begin() + 1, buffer.end() - 1, [](auto value) { return value == 0x11; });
    return assert_true(buffer.front() == 0xA5, test_name, "generate_nmk(target) wrote before target buffer") &&
           assert_true(buffer.back() == 0x5A, test_name, "generate_nmk(target) wrote after target buffer") &&
           assert_true(std::all_of(std::begin(config.session_nmk), std::end(config.session_nmk),
                                   [](auto value) { return value == 0xC3; }),
                       test_name, "generate_nmk(target) modified session_nmk") &&
           assert_true(target_changed, test_name, "generate_nmk(target) left target unchanged");
}

bool test_generate_nmk_uses_full_byte_range() {
    const char* test_name = "test_generate_nmk_uses_full_byte_range";
    EvseSlacConfig config{};
    config.nmk_generation_mode = NmkGenerationMode::full_byte_range;

    for (int attempt = 0; attempt < 16; ++attempt) {
        Nmk nmk{};
        config.generate_nmk(nmk.data());
        if (contains_non_legacy_base36_byte(nmk)) {
            return true;
        }
    }

    return assert_true(false, test_name, "generated NMKs only contained legacy base36 bytes");
}

bool test_generate_nmk_default_mode_is_legacy_printable() {
    const char* test_name = "test_generate_nmk_default_mode_is_legacy_printable";
    EvseSlacConfig config{};

    for (int attempt = 0; attempt < 16; ++attempt) {
        Nmk nmk{};
        config.generate_nmk(nmk.data());
        if (not contains_only_legacy_base36_bytes(nmk)) {
            return assert_true(false, test_name, "default generated NMK contains non-legacy byte");
        }
    }

    return true;
}

bool test_generate_nmk_legacy_printable_mode() {
    const char* test_name = "test_generate_nmk_legacy_printable_mode";
    EvseSlacConfig config{};
    config.nmk_generation_mode = NmkGenerationMode::legacy_printable;

    for (int attempt = 0; attempt < 16; ++attempt) {
        Nmk nmk{};
        config.generate_nmk(nmk.data());
        if (not contains_only_legacy_base36_bytes(nmk)) {
            return assert_true(false, test_name,
                               "generated NMK contains non-legacy byte while in legacy_printable mode");
        }
    }

    return true;
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 5>{
        std::make_pair("test_generate_nmk_updates_session_nmk", test_generate_nmk_updates_session_nmk),
        std::make_pair("test_generate_nmk_writes_target_only", test_generate_nmk_writes_target_only),
        std::make_pair("test_generate_nmk_uses_full_byte_range", test_generate_nmk_uses_full_byte_range),
        std::make_pair("test_generate_nmk_default_mode_is_legacy_printable",
                       test_generate_nmk_default_mode_is_legacy_printable),
        std::make_pair("test_generate_nmk_legacy_printable_mode", test_generate_nmk_legacy_printable_mode),
    };

    int failed_count = 0;
    for (auto const& test : tests) {
        if (not test.second()) {
            std::printf("[FAIL] %s\n", test.first);
            ++failed_count;
        } else {
            std::printf("[PASS] %s\n", test.first);
        }
    }

    if (failed_count > 0) {
        std::printf("FAILED (%d)\n", failed_count);
        return EXIT_FAILURE;
    }

    std::printf("PASSED\n");
    return EXIT_SUCCESS;
}
