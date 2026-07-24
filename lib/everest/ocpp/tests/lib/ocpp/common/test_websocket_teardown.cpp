// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Regression test for the reset-teardown stall in TC_B_49_CS: while the websocket dials a dead
// port, the receive thread parked in SafeQueue::wait_on_queue_element() could miss the interrupt
// wakeup and stall the teardown join for a full poll interval. Drives a real in-flight connection
// to an unresponsive endpoint and asserts disconnect() returns well under the poll interval.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <ocpp/common/websocket/websocket_libwebsockets.hpp>

#include "evse_security_mock.hpp"

using namespace std::chrono_literals;

namespace ocpp {
namespace {

// 198.51.100.0/24 is TEST-NET-2 (RFC 5737): not assigned, so a SYN is dropped and the connection
// attempt stays in flight (state is still "trying"), which is what keeps the receive thread alive
// through teardown. If an environment fails the connect immediately the connection finalizes and
// the teardown returns even sooner, so the test still holds.
constexpr auto DEAD_PORT_URI = "ws://198.51.100.1:9999";

WebsocketConnectionOptions make_options() {
    WebsocketConnectionOptions options;
    options.csms_uri = Uri::parse_and_validate(DEAD_PORT_URI, "cp001", 1);
    options.security_profile = 1;
    options.ocpp_versions = {OcppProtocolVersion::v201};
    options.authorization_key = "DummyAuthorizationKey123";
    options.message_timeout = 5s;
    options.retry_backoff_random_range_s = 0;
    options.retry_backoff_repeat_times = 0;
    options.retry_backoff_wait_minimum_s = 0;
    options.max_connection_attempts = 1;
    options.supported_ciphers_12 = "";
    options.supported_ciphers_13 = "";
    options.ping_interval_s = 0;
    options.ping_payload = "";
    options.pong_timeout_s = 0;
    options.use_ssl_default_verify_paths = false;
    options.verify_csms_common_name = false;
    options.use_tpm_tls = false;
    options.verify_csms_allow_wildcards = false;
    return options;
}

} // namespace

// Acceptance: teardown while a connection to an unresponsive endpoint is in flight must return
// promptly. The receive thread's poll interval is 1 s, so the unfixed code takes about that long
// (the interrupt does not wake the parked receive thread); the fix wakes it at once. The 900 ms
// bound sits safely below the 1 s stall it guards against while staying tolerant of CI scheduling
// jitter (a tight 500 ms bound flaked under load).
TEST(WebsocketTeardown, DisconnectReturnsPromptlyWhileConnectingToDeadPort) {
    auto evse_security = std::make_shared<testing::NiceMock<EvseSecurityMock>>();
    auto ws = std::make_shared<WebsocketLibwebsockets>(make_options(), evse_security);

    ws->register_connected_callback([](OcppProtocolVersion) {});
    ws->register_stopped_connecting_callback([](WebsocketCloseReason) {});
    ws->register_message_callback([](const std::string&) {});

    ASSERT_TRUE(ws->start_connecting());

    // Let the client dial and the receive thread settle into its wait while the connect is in flight.
    std::this_thread::sleep_for(300ms);

    const auto start = std::chrono::steady_clock::now();
    ws->disconnect(WebsocketCloseReason::Normal);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, 900ms) << "disconnect() stalled for "
                              << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                              << " ms: the receive thread was not woken on interrupt";
}

} // namespace ocpp
