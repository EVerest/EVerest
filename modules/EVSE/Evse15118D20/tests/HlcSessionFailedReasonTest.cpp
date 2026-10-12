// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "utils.hpp"

namespace {

using module::charger::hlc_failed_reason_at_teardown;
using module::charger::is_session_stop_message;
using Reason = types::evse_manager::HlcSessionFailedReasonEnum;
using iso15118::V2gMessageType;
using Type20 = iso15118::message_20::Type;
using Type2 = iso15118::message_2::Type;
using TypeDin = iso15118::message_din::Type;

TEST(HlcSessionFailedReasonTest, session_stop_after_authorization_is_an_authorization_failure) {
    // The EV gave up in the authorization loop, e.g. on its V2G_EVCC_Ongoing_Timer, and said goodbye properly
    EXPECT_EQ(
        hlc_failed_reason_at_teardown(V2gMessageType{Type2::SessionStopRes}, V2gMessageType{Type2::AuthorizationRes}),
        Reason::AuthorizationFailed);
    EXPECT_EQ(
        hlc_failed_reason_at_teardown(V2gMessageType{Type20::SessionStopRes}, V2gMessageType{Type20::AuthorizationRes}),
        Reason::AuthorizationFailed);
    EXPECT_EQ(hlc_failed_reason_at_teardown(V2gMessageType{TypeDin::SessionStopRes},
                                            V2gMessageType{TypeDin::ContractAuthenticationRes}),
              Reason::AuthorizationFailed);
    // The ISO 15118-2 SECC answers a SessionStopReq during authorization with a sequence error; the phase
    // before the exchange decides whether the request or the FAILED response was seen last
    EXPECT_EQ(
        hlc_failed_reason_at_teardown(V2gMessageType{Type2::SessionStopReq}, V2gMessageType{Type2::AuthorizationRes}),
        Reason::AuthorizationFailed);
}

TEST(HlcSessionFailedReasonTest, session_stop_before_energy_transfer_names_the_phase_the_ev_left) {
    EXPECT_EQ(hlc_failed_reason_at_teardown(V2gMessageType{Type2::SessionStopRes},
                                            V2gMessageType{Type2::ChargeParameterDiscoveryRes}),
              Reason::ChargingParametersNotAccepted);
    EXPECT_EQ(hlc_failed_reason_at_teardown(V2gMessageType{Type20::SessionStopRes},
                                            V2gMessageType{Type20::ScheduleExchangeRes}),
              Reason::ChargingParametersNotAccepted);
    EXPECT_EQ(
        hlc_failed_reason_at_teardown(V2gMessageType{Type2::SessionStopRes}, V2gMessageType{Type2::CableCheckRes}),
        Reason::EnergyTransferSetupFailed);
    EXPECT_EQ(
        hlc_failed_reason_at_teardown(V2gMessageType{TypeDin::SessionStopRes}, V2gMessageType{TypeDin::PreChargeRes}),
        Reason::EnergyTransferSetupFailed);
}

TEST(HlcSessionFailedReasonTest, session_stop_after_power_delivery_or_welding_detection_is_a_regular_end) {
    EXPECT_EQ(
        hlc_failed_reason_at_teardown(V2gMessageType{Type2::SessionStopRes}, V2gMessageType{Type2::PowerDeliveryRes}),
        std::nullopt);
    EXPECT_EQ(hlc_failed_reason_at_teardown(V2gMessageType{Type2::SessionStopRes},
                                            V2gMessageType{Type2::WeldingDetectionRes}),
              std::nullopt);
    EXPECT_EQ(
        hlc_failed_reason_at_teardown(V2gMessageType{Type20::SessionStopRes}, V2gMessageType{Type20::PowerDeliveryRes}),
        std::nullopt);
    EXPECT_EQ(hlc_failed_reason_at_teardown(V2gMessageType{Type20::SessionStopRes},
                                            V2gMessageType{Type20::DC_WeldingDetectionRes}),
              std::nullopt);
    EXPECT_EQ(hlc_failed_reason_at_teardown(V2gMessageType{TypeDin::SessionStopRes},
                                            V2gMessageType{TypeDin::WeldingDetectionRes}),
              std::nullopt);
    EXPECT_EQ(hlc_failed_reason_at_teardown(V2gMessageType{Type2::SessionStopRes}, std::nullopt), std::nullopt);
}

TEST(HlcSessionFailedReasonTest, connection_closed_without_session_stop_maps_the_last_message) {
    EXPECT_EQ(
        hlc_failed_reason_at_teardown(V2gMessageType{Type2::AuthorizationRes}, V2gMessageType{Type2::AuthorizationRes}),
        Reason::AuthorizationFailed);
    EXPECT_EQ(hlc_failed_reason_at_teardown(V2gMessageType{Type20::DC_ChargeLoopRes},
                                            V2gMessageType{Type20::DC_ChargeLoopRes}),
              Reason::ChargingInterrupted);
    EXPECT_EQ(hlc_failed_reason_at_teardown(V2gMessageType{Type2::WeldingDetectionRes},
                                            V2gMessageType{Type2::WeldingDetectionRes}),
              std::nullopt);
    EXPECT_EQ(hlc_failed_reason_at_teardown(std::nullopt, std::nullopt), std::nullopt);
}

TEST(HlcSessionFailedReasonTest, session_stop_messages_are_recognised_per_protocol) {
    EXPECT_TRUE(is_session_stop_message(V2gMessageType{Type2::SessionStopReq}));
    EXPECT_TRUE(is_session_stop_message(V2gMessageType{Type20::SessionStopRes}));
    EXPECT_TRUE(is_session_stop_message(V2gMessageType{TypeDin::SessionStopReq}));
    EXPECT_FALSE(is_session_stop_message(V2gMessageType{Type2::AuthorizationRes}));
    EXPECT_FALSE(is_session_stop_message(V2gMessageType{Type20::None}));
}

} // namespace
