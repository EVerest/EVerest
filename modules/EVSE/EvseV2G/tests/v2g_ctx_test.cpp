// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <chrono>
#include <memory>
#include <thread>
#include <v2g_ctx.hpp>

#include "ISO15118_chargerImplStub.hpp"
#include "ModuleAdapterStub.hpp"
#include "evse_securityIntfStub.hpp"
#include "iso15118_extensionsImplStub.hpp"
#include "iso15118_vasIntfStub.hpp"
#include "utest_log.hpp"
#include "v2g.hpp"

#include <gtest/gtest.h>

namespace {

struct v2g_contextDeleter {
    void operator()(v2g_context* ptr) const {
        v2g_ctx_free(ptr);
    };
};

class V2gCtxTest : public testing::Test {
protected:
    std::unique_ptr<v2g_context, v2g_contextDeleter> ctx;
    module::stub::QuietModuleAdapterStub adapter;
    module::stub::ISO15118_chargerImplStub charger;
    module::stub::evse_securityIntfStub security;
    module::stub::iso15118_extensionsImplStub extensions;
    module::stub::iso15118_vasIntfStub vas_item;

    V2gCtxTest() : charger(adapter), security(adapter), vas_item(adapter) {
    }

    void v2g_ctx_init_charging_state_cleared() {
        // checks try to match the order is v2g.hpp

        EXPECT_EQ(ctx->last_v2g_msg, V2G_UNKNOWN_MSG);
        EXPECT_EQ(ctx->current_v2g_msg, V2G_UNKNOWN_MSG);
        EXPECT_EQ(ctx->state, 0);

        // not changed
        // is_dc_charger
        // debugMode
        // supported_protocols

        EXPECT_EQ(ctx->selected_protocol, V2G_UNKNOWN_PROTOCOL);
        EXPECT_FALSE(ctx->intl_emergency_shutdown);
        EXPECT_FALSE(ctx->stop_hlc);

        // ctx->is_connection_terminated is updated rather than cleared

        // not changed
        // terminate_connection_on_failed_response
        // contactor_is_closed

        // many items in session not reset
        EXPECT_FALSE(ctx->session.renegotiation_required);
        EXPECT_FALSE(ctx->session.is_charging);
    }

    void SetUp() override {
        auto ptr = v2g_ctx_create(&charger, &extensions, &security, {&vas_item});
        ctx = std::unique_ptr<v2g_context, v2g_contextDeleter>(ptr, v2g_contextDeleter());
        module::stub::clear_logs();
    }

    void TearDown() override {
    }
};

TEST(RunFirst, v2g_ctx_init_charging_values) {
    // must not be part of V2gCtxTest
    // V2gCtxTest::SetUp() creates the v2g_context which would be the 1st
    // call to v2g_ctx_init_charging_values()

    // only called from v2g_ctx_init_charging_session()
    // which is called from v2g_ctx_create()

    // note v2g_ctx_init_charging_values() has a static bool so it
    // performs different tidyup after the first time it is called

    v2g_context ctx;
    ctx.evse_v2g_data.charge_service.FreeService = 9;
    v2g_ctx_init_charging_values(&ctx);
    EXPECT_EQ(ctx.evse_v2g_data.charge_service.FreeService, 0);
    ctx.evse_v2g_data.charge_service.FreeService = 10;
    v2g_ctx_init_charging_values(&ctx);
    EXPECT_EQ(ctx.evse_v2g_data.charge_service.FreeService, 10);

    // reset back to a valid value as it will never be reset
    ctx.evse_v2g_data.charge_service.FreeService = 0;
}

TEST_F(V2gCtxTest, v2g_ctx_init_charging_stateTrue) {
    // called on session start in v2g_handle_connection()

    ctx->last_v2g_msg = V2G_CABLE_CHECK_MSG;
    ctx->current_v2g_msg = V2G_CHARGE_PARAMETER_DISCOVERY_MSG;
    ctx->state = 10;
    ctx->selected_protocol = V2G_PROTO_DIN70121;
    ctx->intl_emergency_shutdown = true;
    ctx->stop_hlc = true;
    ctx->session.renegotiation_required = true;
    ctx->session.is_charging = true;

    v2g_ctx_init_charging_state(ctx.get(), true);

    v2g_ctx_init_charging_state_cleared();
    EXPECT_TRUE(ctx->is_connection_terminated);
}

TEST_F(V2gCtxTest, v2g_ctx_init_charging_stateFalse) {
    // called on session end in v2g_handle_connection()

    ctx->last_v2g_msg = V2G_CABLE_CHECK_MSG;
    ctx->current_v2g_msg = V2G_CHARGE_PARAMETER_DISCOVERY_MSG;
    ctx->state = 10;
    ctx->selected_protocol = V2G_PROTO_DIN70121;
    ctx->intl_emergency_shutdown = true;
    ctx->stop_hlc = true;
    ctx->session.renegotiation_required = true;
    ctx->session.is_charging = true;

    v2g_ctx_init_charging_state(ctx.get(), false);

    v2g_ctx_init_charging_state_cleared();
    EXPECT_FALSE(ctx->is_connection_terminated);
}

#if 0
// v2g_ctx_init_charging_session() is a trivial implementation
TEST_F(V2gCtxTest, v2g_ctx_init_charging_sessionTrue) {
    // called in connection_teardown()
    // calls v2g_ctx_init_charging_state
    // calls v2g_ctx_init_charging_values
}

TEST_F(V2gCtxTest, v2g_ctx_init_charging_sessionFalse) {
    // called in connection_teardown()
    // calls v2g_ctx_init_charging_state
    // calls v2g_ctx_init_charging_values
}
#endif

TEST_F(V2gCtxTest, dc_output_disabled_defaults) {
    EXPECT_EQ(ctx->dc_output_disabled_timeout_ms, V2G_DC_OUTPUT_DISABLED_TIMEOUT);
    EXPECT_FALSE(ctx->dc_output_disabled);
}

TEST_F(V2gCtxTest, dc_output_disabled_confirmation_before_wait_returns_immediately) {
    ctx->is_connection_terminated = false;
    v2g_ctx_request_dc_output_disabled(ctx.get());
    v2g_ctx_signal_dc_output_disabled(ctx.get());

    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(v2g_ctx_wait_dc_output_disabled(ctx.get()));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(500));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
}

TEST_F(V2gCtxTest, dc_output_disabled_wait_is_bounded_by_the_protocol) {
    ctx->is_connection_terminated = false;
    v2g_ctx_request_dc_output_disabled(ctx.get());

    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(v2g_ctx_wait_dc_output_disabled(ctx.get(), 50));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(V2G_DC_OUTPUT_DISABLED_TIMEOUT));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 1);
}

TEST_F(V2gCtxTest, dc_output_disabled_request_clears_earlier_confirmation) {
    // A confirmation left over from an earlier request must not end the wait for a new one
    ctx->is_connection_terminated = false;
    v2g_ctx_signal_dc_output_disabled(ctx.get());
    v2g_ctx_request_dc_output_disabled(ctx.get());
    ctx->dc_output_disabled_timeout_ms = 50;

    EXPECT_FALSE(v2g_ctx_wait_dc_output_disabled(ctx.get()));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 1);
}

TEST_F(V2gCtxTest, dc_output_disabled_confirmation_wakes_up_waiter) {
    ctx->is_connection_terminated = false;
    v2g_ctx_request_dc_output_disabled(ctx.get());

    std::thread confirmer([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        v2g_ctx_signal_dc_output_disabled(ctx.get());
    });

    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(v2g_ctx_wait_dc_output_disabled(ctx.get()));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(V2G_DC_OUTPUT_DISABLED_TIMEOUT));
    confirmer.join();
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
}

TEST_F(V2gCtxTest, dc_output_disabled_wait_ends_on_hlc_stop) {
    ctx->is_connection_terminated = false;
    v2g_ctx_request_dc_output_disabled(ctx.get());
    ctx->stop_hlc = true;

    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(v2g_ctx_wait_dc_output_disabled(ctx.get()));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(500));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
}

TEST_F(V2gCtxTest, dc_output_disabled_no_wait_with_zero_timeout) {
    ctx->is_connection_terminated = false;
    v2g_ctx_request_dc_output_disabled(ctx.get());
    ctx->dc_output_disabled_timeout_ms = 0;

    EXPECT_FALSE(v2g_ctx_wait_dc_output_disabled(ctx.get()));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
}

TEST_F(V2gCtxTest, dc_output_disabled_wait_ends_on_terminated_connection) {
    v2g_ctx_request_dc_output_disabled(ctx.get());
    ctx->is_connection_terminated = true;

    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(v2g_ctx_wait_dc_output_disabled(ctx.get()));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(500));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
}

TEST_F(V2gCtxTest, dc_output_disabled_wait_is_accounted_as_response_wait) {
    ctx->is_connection_terminated = false;
    ctx->response_wait_ms = 0;
    v2g_ctx_request_dc_output_disabled(ctx.get());

    std::thread confirmer([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        v2g_ctx_signal_dc_output_disabled(ctx.get());
    });

    EXPECT_TRUE(v2g_ctx_wait_dc_output_disabled(ctx.get()));
    confirmer.join();
    EXPECT_GE(ctx->response_wait_ms, 90);
    EXPECT_LT(ctx->response_wait_ms, V2G_DC_OUTPUT_DISABLED_TIMEOUT);
}

TEST_F(V2gCtxTest, dc_output_disabled_wait_wakes_up_on_terminated_connection) {
    ctx->is_connection_terminated = false;
    v2g_ctx_request_dc_output_disabled(ctx.get());

    std::thread unplug([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ctx->is_connection_terminated = true;
        v2g_ctx_wake_waiters(ctx.get());
    });

    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(v2g_ctx_wait_dc_output_disabled(ctx.get()));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(1000));
    unplug.join();
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
}

TEST_F(V2gCtxTest, dc_output_disabled_wait_wakes_up_on_hlc_stop) {
    ctx->is_connection_terminated = false;
    v2g_ctx_request_dc_output_disabled(ctx.get());

    std::thread stopper([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ctx->stop_hlc = true;
        v2g_ctx_wake_waiters(ctx.get());
    });

    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(v2g_ctx_wait_dc_output_disabled(ctx.get()));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(1000));
    stopper.join();
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
}

TEST_F(V2gCtxTest, present_values_defaults) {
    EXPECT_EQ(ctx->present_values_timeout_ms, V2G_PRESENT_VALUES_TIMEOUT);
    EXPECT_EQ(ctx->present_values_count, 0);
}

TEST_F(V2gCtxTest, present_values_after_the_request_end_the_wait_at_once) {
    ctx->is_connection_terminated = false;
    const unsigned int at_request = ctx->present_values_count;
    v2g_ctx_signal_present_values(ctx.get());

    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(v2g_ctx_wait_present_values(ctx.get(), at_request));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(500));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
}

TEST_F(V2gCtxTest, present_values_wait_wakes_up_on_new_values) {
    ctx->is_connection_terminated = false;
    ctx->response_wait_ms = 0;
    v2g_ctx_signal_present_values(ctx.get()); // from before the request
    const unsigned int at_request = ctx->present_values_count;

    std::thread meter([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        v2g_ctx_signal_present_values(ctx.get());
    });

    EXPECT_TRUE(v2g_ctx_wait_present_values(ctx.get(), at_request));
    meter.join();
    EXPECT_GE(ctx->response_wait_ms, 90);
    EXPECT_LT(ctx->response_wait_ms, V2G_PRESENT_VALUES_TIMEOUT);
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
}

TEST_F(V2gCtxTest, present_values_wait_is_bounded) {
    ctx->is_connection_terminated = false;
    ctx->present_values_timeout_ms = 50;

    EXPECT_FALSE(v2g_ctx_wait_present_values(ctx.get(), ctx->present_values_count));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 1);
}

TEST_F(V2gCtxTest, present_values_no_wait_with_zero_timeout) {
    ctx->is_connection_terminated = false;
    ctx->present_values_timeout_ms = 0;

    EXPECT_FALSE(v2g_ctx_wait_present_values(ctx.get(), ctx->present_values_count));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
}

TEST_F(V2gCtxTest, present_values_wait_wakes_up_on_terminated_connection) {
    ctx->is_connection_terminated = false;

    std::thread unplug([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ctx->is_connection_terminated = true;
        v2g_ctx_wake_waiters(ctx.get());
    });

    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(v2g_ctx_wait_present_values(ctx.get(), ctx->present_values_count));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(1000));
    unplug.join();
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
}

TEST(valgrind, memcheck) {
    // v2g_ctx_free() now sets shutdown and joins the event thread,
    // so there is no use-after-free during cleanup.

    // run via valgrind to ensure that malloc/free are working
    module::stub::QuietModuleAdapterStub adapter;
    module::stub::ISO15118_chargerImplStub charger(adapter);
    module::stub::evse_securityIntfStub security(adapter);
    module::stub::iso15118_extensionsImplStub extensions;
    module::stub::iso15118_vasIntfStub vas_item(adapter);

    auto ptr = v2g_ctx_create(&charger, &extensions, &security, {&vas_item});
    v2g_ctx_free(ptr);
}

} // namespace
