// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso_server.hpp>

#include <gtest/gtest.h>

#include "ISO15118_chargerImplStub.hpp"
#include "utest_log.hpp"
#include "v2g.hpp"
#include "v2g_ctx.hpp"

#include <chrono>
#include <memory>
#include <thread>

uint64_t v2g_session_id_from_exi(bool is_iso, void* exi_in) {
    return 0;
}

namespace {

constexpr uint8_t offered_sa_schedule_tuple_id = 1;
constexpr uint8_t unknown_sa_schedule_tuple_id = 99;

class IsoServerPowerDeliveryTest : public testing::Test {
protected:
    std::unique_ptr<v2g_connection> conn;
    std::unique_ptr<v2g_context> ctx;
    std::unique_ptr<iso2_exiDocument> exi_in;
    std::unique_ptr<iso2_exiDocument> exi_out;

    module::stub::QuietModuleAdapterStub adapter;
    module::stub::ISO15118_chargerImplStub charger;

    IsoServerPowerDeliveryTest() : charger(adapter) {
    }

    void SetUp() override {
        conn = std::make_unique<v2g_connection>();
        ctx = std::make_unique<v2g_context>();
        exi_in = std::make_unique<iso2_exiDocument>();
        exi_out = std::make_unique<iso2_exiDocument>();

        module::stub::clear_logs();
        conn->ctx = ctx.get();
        conn->ctx->p_charger = &charger;
        conn->exi_in.iso2EXIDocument = exi_in.get();
        conn->exi_out.iso2EXIDocument = exi_out.get();

        // Same SAScheduleList the module offers in ChargeParameterDiscoveryRes
        v2g_ctx_init_charging_values(ctx.get());

        ctx->is_dc_charger = false;
        ctx->contactor_is_closed = true;
        ctx->last_v2g_msg = V2G_CHARGE_PARAMETER_DISCOVERY_MSG;
        ctx->state = static_cast<int>(iso_ac_state_id::WAIT_FOR_POWERDELIVERY);

        init_iso2_exiDocument(exi_in.get());
        exi_in->V2G_Message.Body.PowerDeliveryReq_isUsed = 1u;
        auto& req = exi_in->V2G_Message.Body.PowerDeliveryReq;
        init_iso2_PowerDeliveryReqType(&req);
        req.ChargeProgress = iso2_chargeProgressType_Start;
    }

    void set_charging_profile(int16_t max_power_w) {
        auto& req = exi_in->V2G_Message.Body.PowerDeliveryReq;
        req.ChargingProfile_isUsed = 1u;
        req.ChargingProfile.ProfileEntry.arrayLen = 1;
        auto& entry = req.ChargingProfile.ProfileEntry.array[0];
        entry.ChargingProfileEntryStart = 0;
        entry.ChargingProfileEntryMaxPower.Value = max_power_w;
        entry.ChargingProfileEntryMaxPower.Multiplier = 0;
        entry.ChargingProfileEntryMaxPower.Unit = iso2_unitSymbolType_W;
    }

    void set_offered_pmax(int16_t pmax_w) {
        auto& pmax = ctx->evse_v2g_data.evse_sa_schedule_list.SAScheduleTuple.array[0]
                         .PMaxSchedule.PMaxScheduleEntry.array[0]
                         .PMax;
        pmax.Value = pmax_w;
        pmax.Multiplier = 0;
    }

    const iso2_PowerDeliveryResType& response() const {
        return exi_out->V2G_Message.Body.PowerDeliveryRes;
    }
};

TEST_F(IsoServerPowerDeliveryTest, offered_list_has_single_tuple) {
    const auto& tuples = ctx->evse_v2g_data.evse_sa_schedule_list.SAScheduleTuple;
    EXPECT_EQ(tuples.arrayLen, 1);
    EXPECT_EQ(tuples.array[0].SAScheduleTupleID, offered_sa_schedule_tuple_id);
}

TEST_F(IsoServerPowerDeliveryTest, unknown_tuple_id_with_charging_profile_is_rejected) {
    // [V2G2-479]
    exi_in->V2G_Message.Body.PowerDeliveryReq.SAScheduleTupleID = unknown_sa_schedule_tuple_id;
    set_charging_profile(11000);
    ctx->session.sa_schedule_tuple_id = offered_sa_schedule_tuple_id;

    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);

    EXPECT_EQ(exi_out->V2G_Message.Body.PowerDeliveryRes_isUsed, 1u);
    EXPECT_EQ(response().ResponseCode, iso2_responseCodeType_FAILED_TariffSelectionInvalid);
    EXPECT_EQ(ctx->session.sa_schedule_tuple_id, offered_sa_schedule_tuple_id);
}

TEST_F(IsoServerPowerDeliveryTest, unknown_tuple_id_with_full_tuple_list_is_rejected) {
    // [V2G2-479]
    auto& tuples = ctx->evse_v2g_data.evse_sa_schedule_list.SAScheduleTuple;
    for (uint8_t idx = 1; idx < iso2_SAScheduleTupleType_3_ARRAY_SIZE; idx++) {
        tuples.array[idx] = tuples.array[0];
        tuples.array[idx].SAScheduleTupleID = offered_sa_schedule_tuple_id + idx;
    }
    tuples.arrayLen = iso2_SAScheduleTupleType_3_ARRAY_SIZE;
    exi_in->V2G_Message.Body.PowerDeliveryReq.SAScheduleTupleID = unknown_sa_schedule_tuple_id;
    set_charging_profile(11000);

    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);

    EXPECT_EQ(response().ResponseCode, iso2_responseCodeType_FAILED_TariffSelectionInvalid);
}

TEST_F(IsoServerPowerDeliveryTest, unknown_tuple_id_without_charging_profile_is_rejected) {
    // [V2G2-479]
    exi_in->V2G_Message.Body.PowerDeliveryReq.SAScheduleTupleID = unknown_sa_schedule_tuple_id;

    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);

    EXPECT_EQ(response().ResponseCode, iso2_responseCodeType_FAILED_TariffSelectionInvalid);
}

TEST_F(IsoServerPowerDeliveryTest, offered_tuple_id_with_charging_profile_within_pmax_is_accepted) {
    exi_in->V2G_Message.Body.PowerDeliveryReq.SAScheduleTupleID = offered_sa_schedule_tuple_id;
    set_offered_pmax(11000);
    set_charging_profile(11000);

    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);

    EXPECT_EQ(response().ResponseCode, iso2_responseCodeType_OK);
    EXPECT_EQ(ctx->session.sa_schedule_tuple_id, offered_sa_schedule_tuple_id);
    EXPECT_EQ(ctx->state, static_cast<int>(iso_ac_state_id::WAIT_FOR_CHARGINGSTATUS));
}

TEST_F(IsoServerPowerDeliveryTest, offered_tuple_id_with_charging_profile_above_pmax_is_rejected) {
    // [V2G2-224]
    exi_in->V2G_Message.Body.PowerDeliveryReq.SAScheduleTupleID = offered_sa_schedule_tuple_id;
    set_offered_pmax(11000);
    set_charging_profile(22000);

    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);

    EXPECT_EQ(response().ResponseCode, iso2_responseCodeType_FAILED_ChargingProfileInvalid);
}

class IsoServerDcStopTest : public IsoServerPowerDeliveryTest {
protected:
    void SetUp() override {
        IsoServerPowerDeliveryTest::SetUp();

        // The fixture builds the context without v2g_ctx_create(), so set up the
        // condition variable with the monotonic clock the wait helper relies on
        pthread_mutex_init(&ctx->mqtt_lock, nullptr);
        pthread_condattr_init(&ctx->mqtt_attr);
        pthread_condattr_setclock(&ctx->mqtt_attr, CLOCK_MONOTONIC);
        pthread_cond_init(&ctx->mqtt_cond, &ctx->mqtt_attr);

        ctx->is_dc_charger = true;
        ctx->last_v2g_msg = V2G_CURRENT_DEMAND_MSG;
        ctx->state = static_cast<int>(iso_dc_state_id::WAIT_FOR_CURRENTDEMAND_POWERDELIVERY);
        ctx->dc_output_disabled = true; // left over from an earlier stop, must not end the wait

        auto& req = exi_in->V2G_Message.Body.PowerDeliveryReq;
        req.ChargeProgress = iso2_chargeProgressType_Stop;
        req.SAScheduleTupleID = offered_sa_schedule_tuple_id;
    }

    void TearDown() override {
        pthread_cond_destroy(&ctx->mqtt_cond);
        pthread_condattr_destroy(&ctx->mqtt_attr);
        pthread_mutex_destroy(&ctx->mqtt_lock);
    }
};

TEST_F(IsoServerDcStopTest, stop_response_waits_for_dc_output_disabled) {
    // [IEC 61851-23:2023 Table CC.9 t103, t104] Side B is disabled before the PowerDeliveryRes
    ctx->dc_output_disabled_timeout_ms = 2000;
    constexpr auto confirmation_delay = std::chrono::milliseconds(100);

    std::thread confirmer([this, confirmation_delay]() {
        std::this_thread::sleep_for(confirmation_delay);
        v2g_ctx_signal_dc_output_disabled(ctx.get());
    });

    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    confirmer.join();

    EXPECT_GE(elapsed, confirmation_delay);
    EXPECT_LT(elapsed, std::chrono::milliseconds(ctx->dc_output_disabled_timeout_ms));
    EXPECT_EQ(response().ResponseCode, iso2_responseCodeType_OK);
    EXPECT_EQ(ctx->state, static_cast<int>(iso_dc_state_id::WAIT_FOR_WELDINGDETECTION_SESSIONSTOP));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
}

TEST_F(IsoServerDcStopTest, stop_response_sent_after_timeout_without_confirmation) {
    ctx->dc_output_disabled_timeout_ms = 50;

    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);

    EXPECT_EQ(response().ResponseCode, iso2_responseCodeType_OK);
    EXPECT_EQ(ctx->state, static_cast<int>(iso_dc_state_id::WAIT_FOR_WELDINGDETECTION_SESSIONSTOP));
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 1);
}

class IsoServerWeldingDetectionTest : public IsoServerDcStopTest {
protected:
    void SetUp() override {
        IsoServerDcStopTest::SetUp();

        ctx->last_v2g_msg = V2G_POWER_DELIVERY_MSG;
        ctx->state = static_cast<int>(iso_dc_state_id::WAIT_FOR_WELDINGDETECTION_SESSIONSTOP);
        set_present_voltage(400);

        exi_in->V2G_Message.Body.PowerDeliveryReq_isUsed = 0u;
        exi_in->V2G_Message.Body.WeldingDetectionReq_isUsed = 1u;
        init_iso2_WeldingDetectionReqType(&exi_in->V2G_Message.Body.WeldingDetectionReq);
    }

    void set_present_voltage(int16_t voltage) {
        auto& present_voltage = ctx->evse_v2g_data.evse_present_voltage;
        present_voltage.Value = voltage;
        present_voltage.Multiplier = 0;
        present_voltage.Unit = iso2_unitSymbolType_V;
    }

    const iso2_WeldingDetectionResType& welding_response() const {
        return exi_out->V2G_Message.Body.WeldingDetectionRes;
    }
};

TEST_F(IsoServerWeldingDetectionTest, response_waits_for_present_values_after_the_request) {
    // [IEC 61851-23:2023 CC.6.3, CC.7.6.17] EVSEPresentVoltage measured after the WeldingDetectionReq. The
    // first update after the request can carry a sample taken just before it (a polled meter delivers a
    // sample up to one poll interval later): a 32 V sample taken 50 ms before the request arrives 50 ms
    // after it, the 0 V sample taken after the request follows. The response carries the second one.
    ctx->present_values_timeout_ms = 2000;
    constexpr auto measurement_delay = std::chrono::milliseconds(100);

    std::thread meter([this, measurement_delay]() {
        std::this_thread::sleep_for(measurement_delay / 2);
        set_present_voltage(32);
        v2g_ctx_signal_present_values(ctx.get());
        std::this_thread::sleep_for(measurement_delay / 2);
        set_present_voltage(12);
        v2g_ctx_signal_present_values(ctx.get());
    });

    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    meter.join();

    EXPECT_GE(elapsed, measurement_delay);
    EXPECT_LT(elapsed, std::chrono::milliseconds(ctx->present_values_timeout_ms));
    EXPECT_EQ(exi_out->V2G_Message.Body.WeldingDetectionRes_isUsed, 1u);
    EXPECT_EQ(welding_response().ResponseCode, iso2_responseCodeType_OK);
    EXPECT_EQ(welding_response().EVSEPresentVoltage.Value, 12);
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
}

TEST_F(IsoServerWeldingDetectionTest, response_sent_with_the_one_update_after_timeout) {
    ctx->present_values_timeout_ms = 100;

    std::thread meter([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        set_present_voltage(32);
        v2g_ctx_signal_present_values(ctx.get());
    });

    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);
    meter.join();

    EXPECT_EQ(welding_response().ResponseCode, iso2_responseCodeType_OK);
    EXPECT_EQ(welding_response().EVSEPresentVoltage.Value, 32);
    // One update is not a stalled source: no warning
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 0);
}

TEST_F(IsoServerWeldingDetectionTest, response_sent_with_last_values_after_timeout) {
    // Present values from before the request do not count
    v2g_ctx_signal_present_values(ctx.get());
    ctx->present_values_timeout_ms = 50;

    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);

    EXPECT_EQ(welding_response().ResponseCode, iso2_responseCodeType_OK);
    EXPECT_EQ(welding_response().EVSEPresentVoltage.Value, 400);
    EXPECT_EQ(module::stub::get_logs(dloglevel_t::DLOG_LEVEL_WARNING).size(), 1);
}

} // namespace
