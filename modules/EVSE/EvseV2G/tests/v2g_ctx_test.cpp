// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <v2g_ctx.hpp>
#include <vector>

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

struct RecordingModuleAdapterStub : public module::stub::QuietModuleAdapterStub {
    void publish_fn(const std::string&, const std::string& var, Value value) override {
        if (var == "welding_detection") {
            welding_detection.push_back(value.get<std::string>());
        }
    }
    std::vector<std::string> welding_detection;
};

class V2gCtxWeldingDetectionTest : public testing::Test {
protected:
    std::unique_ptr<v2g_context, v2g_contextDeleter> ctx;
    RecordingModuleAdapterStub adapter;
    module::stub::ISO15118_chargerImplStub charger;
    module::stub::evse_securityIntfStub security;
    module::stub::iso15118_extensionsImplStub extensions;
    module::stub::iso15118_vasIntfStub vas_item;

    V2gCtxWeldingDetectionTest() : charger(adapter), security(adapter), vas_item(adapter) {
    }

    void SetUp() override {
        auto ptr = v2g_ctx_create(&charger, &extensions, &security, {&vas_item});
        ctx = std::unique_ptr<v2g_context, v2g_contextDeleter>(ptr, v2g_contextDeleter());
        // A connection is up, as v2g_handle_connection() sets it.
        v2g_ctx_init_charging_state(ctx.get(), false);
        module::stub::clear_logs();
    }

    // What ISO15118_chargerImpl::handle_update_dc_present_values does for a marked value.
    void deliver_welding_voltage() {
        pthread_mutex_lock(&ctx->mqtt_lock);
        if (ctx->session.welding_detection_active) {
            ctx->session.welding_voltage_updates++;
            pthread_cond_signal(&ctx->mqtt_cond);
        }
        pthread_mutex_unlock(&ctx->mqtt_lock);
    }

    // What ISO15118_chargerImpl::handle_contactor_closed does.
    void report_contactor(bool closed) {
        pthread_mutex_lock(&ctx->mqtt_lock);
        ctx->contactor_is_closed = closed;
        pthread_cond_signal(&ctx->mqtt_cond);
        pthread_mutex_unlock(&ctx->mqtt_lock);
    }
};

TEST_F(V2gCtxWeldingDetectionTest, start_and_finish_publish_once) {
    v2g_ctx_start_welding_detection(ctx.get());
    EXPECT_TRUE(ctx->session.welding_detection_active);

    v2g_ctx_finish_welding_detection(ctx.get());
    v2g_ctx_finish_welding_detection(ctx.get());

    EXPECT_FALSE(ctx->session.welding_detection_active);
    EXPECT_EQ(adapter.welding_detection, (std::vector<std::string>{"Started", "Finished"}));
}

TEST_F(V2gCtxWeldingDetectionTest, finish_without_start_publishes_nothing) {
    v2g_ctx_finish_welding_detection(ctx.get());
    EXPECT_TRUE(adapter.welding_detection.empty());
}

TEST_F(V2gCtxWeldingDetectionTest, wait_returns_on_the_second_value_after_the_request) {
    v2g_ctx_start_welding_detection(ctx.get());
    // Values from before the request do not count: the EV may have switched its disconnection device since.
    deliver_welding_voltage();
    deliver_welding_voltage();

    std::thread mqtt([this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        deliver_welding_voltage();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        deliver_welding_voltage();
    });
    const auto start = std::chrono::steady_clock::now();
    v2g_ctx_wait_for_welding_voltage(ctx.get());
    const auto waited = std::chrono::steady_clock::now() - start;
    mqtt.join();

    EXPECT_GE(waited, std::chrono::milliseconds(100));
    EXPECT_LT(waited, std::chrono::milliseconds(V2G_WELDING_VOLTAGE_TIMEOUT));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_INFO).size(), 0);
    EXPECT_GT(ctx->response_wait_ms, 0);
}

TEST_F(V2gCtxWeldingDetectionTest, wait_times_out_and_warns_without_any_value) {
    v2g_ctx_start_welding_detection(ctx.get());

    const auto start = std::chrono::steady_clock::now();
    v2g_ctx_wait_for_welding_voltage(ctx.get());
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(V2G_WELDING_VOLTAGE_TIMEOUT));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 1);
}

TEST_F(V2gCtxWeldingDetectionTest, wait_times_out_with_one_value_and_logs_info) {
    v2g_ctx_start_welding_detection(ctx.get());

    std::thread mqtt([this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        deliver_welding_voltage();
    });
    const auto start = std::chrono::steady_clock::now();
    v2g_ctx_wait_for_welding_voltage(ctx.get());
    mqtt.join();
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(V2G_WELDING_VOLTAGE_TIMEOUT));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_INFO).size(), 1);
}

TEST_F(V2gCtxWeldingDetectionTest, every_request_waits) {
    v2g_ctx_start_welding_detection(ctx.get());
    deliver_welding_voltage();
    deliver_welding_voltage();
    std::thread mqtt([this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        deliver_welding_voltage();
        deliver_welding_voltage();
    });
    v2g_ctx_wait_for_welding_voltage(ctx.get());
    mqtt.join();

    // The second request waits again: its values have to postdate it.
    const auto again = std::chrono::steady_clock::now();
    v2g_ctx_wait_for_welding_voltage(ctx.get());
    EXPECT_GE(std::chrono::steady_clock::now() - again, std::chrono::milliseconds(V2G_WELDING_VOLTAGE_TIMEOUT));
}

TEST_F(V2gCtxWeldingDetectionTest, wait_outside_the_phase_returns_at_once) {
    const auto start = std::chrono::steady_clock::now();
    v2g_ctx_wait_for_welding_voltage(ctx.get());
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(V2G_WELDING_VOLTAGE_TIMEOUT));
}

TEST_F(V2gCtxWeldingDetectionTest, stop_response_waits_for_the_open_contactor) {
    report_contactor(true);

    std::thread mqtt([this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        report_contactor(false);
    });
    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(v2g_ctx_wait_contactor_open(ctx.get(), V2G_CONTACTOR_OPEN_TIMEOUT_DIN));
    const auto waited = std::chrono::steady_clock::now() - start;
    mqtt.join();

    EXPECT_GE(waited, std::chrono::milliseconds(50));
    EXPECT_LT(waited, std::chrono::milliseconds(V2G_CONTACTOR_OPEN_TIMEOUT_DIN));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
    EXPECT_GT(ctx->response_wait_ms, 0);
}

TEST_F(V2gCtxWeldingDetectionTest, stop_response_does_not_wait_for_an_open_contactor) {
    report_contactor(false);

    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(v2g_ctx_wait_contactor_open(ctx.get(), V2G_CONTACTOR_OPEN_TIMEOUT_DIN));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(50));
}

TEST_F(V2gCtxWeldingDetectionTest, stop_response_goes_out_anyway_after_the_timeout) {
    report_contactor(true);

    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(v2g_ctx_wait_contactor_open(ctx.get(), 100));
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(100));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 1);
}

TEST_F(V2gCtxWeldingDetectionTest, stop_response_wait_ends_on_emergency_shutdown) {
    report_contactor(true);

    std::thread mqtt([this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pthread_mutex_lock(&ctx->mqtt_lock);
        ctx->intl_emergency_shutdown = true;
        pthread_cond_signal(&ctx->mqtt_cond);
        pthread_mutex_unlock(&ctx->mqtt_lock);
    });
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(v2g_ctx_wait_contactor_open(ctx.get(), V2G_CONTACTOR_OPEN_TIMEOUT_DIN));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(V2G_CONTACTOR_OPEN_TIMEOUT_DIN));
    mqtt.join();
}

// What the writers of these flags do: set under mqtt_lock and signal the connection thread's waits.
class V2gCtxWeldingDetectionWakeUpTest : public V2gCtxWeldingDetectionTest,
                                         public testing::WithParamInterface<std::atomic_bool v2g_context::*> {
protected:
    void wait_wakes_up_on_the_flag() {
        v2g_ctx_start_welding_detection(ctx.get());

        std::thread mqtt([this] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            pthread_mutex_lock(&ctx->mqtt_lock);
            (*ctx).*GetParam() = true;
            pthread_cond_signal(&ctx->mqtt_cond);
            pthread_mutex_unlock(&ctx->mqtt_lock);
        });
        const auto start = std::chrono::steady_clock::now();
        v2g_ctx_wait_for_welding_voltage(ctx.get());
        const auto waited = std::chrono::steady_clock::now() - start;
        mqtt.join();

        EXPECT_GE(waited, std::chrono::milliseconds(50));
        EXPECT_LT(waited, std::chrono::milliseconds(V2G_WELDING_VOLTAGE_TIMEOUT));
    }
};

TEST_P(V2gCtxWeldingDetectionWakeUpTest, wait_wakes_up_on_the_flag) {
    wait_wakes_up_on_the_flag();
}

INSTANTIATE_TEST_SUITE_P(shutdown_flags, V2gCtxWeldingDetectionWakeUpTest,
                         testing::Values(&v2g_context::intl_emergency_shutdown, &v2g_context::stop_hlc,
                                         &v2g_context::is_connection_terminated));

TEST_F(V2gCtxWeldingDetectionTest, init_charging_state_ends_the_phase) {
    v2g_ctx_start_welding_detection(ctx.get());
    deliver_welding_voltage();

    // A new connection on a phase the previous one left open.
    v2g_ctx_init_charging_state(ctx.get(), false);

    EXPECT_FALSE(ctx->session.welding_detection_active);
    EXPECT_EQ(ctx->session.welding_voltage_updates, 0);
    EXPECT_EQ(adapter.welding_detection, (std::vector<std::string>{"Started", "Finished"}));
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
