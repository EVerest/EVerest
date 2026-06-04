// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "test_framework_transportImpl.hpp"

#include <chrono>
#include <thread>

namespace module {
namespace main {

namespace {
constexpr auto SAMPLE_VALUE = "framework-transport-variable-value";
constexpr auto REQUEST_VALUE = "framework-transport-command-request";
constexpr auto RESPONSE_VALUE = "framework-transport-command-response";
} // namespace

void test_framework_transportImpl::init() {
}

void test_framework_transportImpl::ready() {
    publish_sample_value(SAMPLE_VALUE);
    EVLOG_info << "FRAMEWORK_TRANSPORT_PROVIDER_VAR value=" << SAMPLE_VALUE;
    std::thread([this]() {
        for (auto attempt = 0; attempt < 20; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            publish_sample_value(SAMPLE_VALUE);
        }
    }).detach();
}

std::string test_framework_transportImpl::handle_roundtrip(std::string& payload) {
    if (payload != REQUEST_VALUE) {
        EVLOG_error << "FRAMEWORK_TRANSPORT_PROVIDER_CMD unexpected_request=" << payload;
        return "unexpected-request";
    }

    EVLOG_info << "FRAMEWORK_TRANSPORT_PROVIDER_CMD request=" << payload << " response=" << RESPONSE_VALUE;
    return RESPONSE_VALUE;
}

} // namespace main
} // namespace module
