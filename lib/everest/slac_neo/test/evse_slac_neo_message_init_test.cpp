// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <net/ethernet.h>
#include <utility>

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/slac_messages.hpp>

using namespace everest::lib::slac;

namespace {

constexpr std::array<std::uint8_t, 3> lumissil_vendor_mme{{0x00, 0x16, 0xE8}};

bool assert_true(bool cond, const char* test_name, const char* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

template <typename RequestT> bool has_lumissil_vendor_mme(RequestT const& request) {
    return std::equal(lumissil_vendor_mme.begin(), lumissil_vendor_mme.end(), std::begin(request.vendor_mme));
}

bool lms_header_is_zeroed(messages::lumissil::lms_header const& lms) {
    auto const* first = reinterpret_cast<std::uint8_t const*>(&lms);
    auto const* last = first + sizeof(lms);
    return std::all_of(first, last, [](auto value) { return value == 0; });
}

bool test_default_lumissil_get_version_req_is_initialized() {
    const char* test_name = "test_default_lumissil_get_version_req_is_initialized";
    messages::lumissil::nscm_get_version_req request;

    return assert_true(has_lumissil_vendor_mme(request), test_name, "vendor MME bytes not initialized") &&
           assert_true(lms_header_is_zeroed(request.lms), test_name, "LMS header not zero-initialized");
}

bool test_default_lumissil_link_status_req_is_initialized() {
    const char* test_name = "test_default_lumissil_link_status_req_is_initialized";
    messages::lumissil::nscm_get_d_link_status_req request;

    return assert_true(has_lumissil_vendor_mme(request), test_name, "vendor MME bytes not initialized") &&
           assert_true(lms_header_is_zeroed(request.lms), test_name, "LMS header not zero-initialized");
}

bool test_default_lumissil_reset_req_is_initialized() {
    const char* test_name = "test_default_lumissil_reset_req_is_initialized";
    messages::lumissil::nscm_reset_device_req request;

    return assert_true(has_lumissil_vendor_mme(request), test_name, "vendor MME bytes not initialized") &&
           assert_true(lms_header_is_zeroed(request.lms), test_name, "LMS header not zero-initialized") &&
           assert_true(request.mode == 0, test_name, "reset mode not initialized");
}

bool test_default_homeplug_message_is_invalid() {
    const char* test_name = "test_default_homeplug_message_is_invalid";
    messages::HomeplugMessage message;

    return assert_true(not message.is_valid(), test_name, "default message marked valid") &&
           assert_true(message.frame_size() == 0, test_name, "default message has unexpected frame_size");
}

bool test_setup_payload_is_valid_with_sane_frame_size() {
    const char* test_name = "test_setup_payload_is_valid_with_sane_frame_size";
    messages::cm_set_key_req payload{};
    messages::HomeplugMessage message;
    message.setup_payload(&payload, sizeof(payload), defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_REQ, defs::MMV::AV_1_0);

    return assert_true(message.is_valid(), test_name, "setup payload message invalid") &&
           assert_true(message.frame_size() >= static_cast<std::size_t>(defs::MME_MIN_LENGTH), test_name,
                       "frame_size below minimum allowed") &&
           assert_true(message.frame_size() <= ETH_FRAME_LEN, test_name, "frame_size exceeds ETH_FRAME_LEN");
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 5>{
        std::make_pair("test_default_lumissil_get_version_req_is_initialized",
                       test_default_lumissil_get_version_req_is_initialized),
        std::make_pair("test_default_lumissil_link_status_req_is_initialized",
                       test_default_lumissil_link_status_req_is_initialized),
        std::make_pair("test_default_lumissil_reset_req_is_initialized", test_default_lumissil_reset_req_is_initialized),
        std::make_pair("test_default_homeplug_message_is_invalid", test_default_homeplug_message_is_invalid),
        std::make_pair("test_setup_payload_is_valid_with_sane_frame_size",
                       test_setup_payload_is_valid_with_sane_frame_size),
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
