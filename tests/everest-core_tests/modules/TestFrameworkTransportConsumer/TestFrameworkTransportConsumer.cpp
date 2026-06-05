// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "TestFrameworkTransportConsumer.hpp"

namespace module {

namespace {
constexpr auto SAMPLE_VALUE = "framework-transport-variable-value";
constexpr auto REQUEST_VALUE = "framework-transport-command-request";
constexpr auto RESPONSE_VALUE = "framework-transport-command-response";
} // namespace

void TestFrameworkTransportConsumer::init() {
    r_provider->subscribe_sample_value([this](const std::string& value) {
        {
            std::lock_guard<std::mutex> lock(result_mutex);
            observed_variable = value;
            variable_ok = (value == SAMPLE_VALUE);
        }

        EVLOG_info << "FRAMEWORK_TRANSPORT_VAR value=" << value;
        maybe_log_success();
    });
}

void TestFrameworkTransportConsumer::ready() {
    const auto response = r_provider->call_roundtrip(REQUEST_VALUE);
    {
        std::lock_guard<std::mutex> lock(result_mutex);
        command_response = response;
        command_ok = (response == RESPONSE_VALUE);
    }

    EVLOG_info << "FRAMEWORK_TRANSPORT_CMD request=" << REQUEST_VALUE << " response=" << response;
    maybe_log_success();
}

void TestFrameworkTransportConsumer::maybe_log_success() {
    std::lock_guard<std::mutex> lock(result_mutex);
    if (success_logged || !variable_ok || !command_ok) {
        return;
    }

    success_logged = true;
    EVLOG_info << "FRAMEWORK_TRANSPORT_SUCCESS variable=" << observed_variable << " response=" << command_response;
}

} // namespace module
