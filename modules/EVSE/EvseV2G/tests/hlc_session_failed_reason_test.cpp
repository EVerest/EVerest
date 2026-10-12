// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <hlc_session_failed_reason.hpp>

#include <gtest/gtest.h>

namespace {

using Reason = types::evse_manager::HlcSessionFailedReasonEnum;

TEST(HlcSessionFailedReasonTest, session_stop_after_authorization_is_an_authorization_failure) {
    // The EV gave up in the authorization loop, e.g. on its V2G_EVCC_Ongoing_Timer, and said goodbye properly
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_SESSION_STOP_MSG, V2G_AUTHORIZATION_MSG, false),
              Reason::AuthorizationFailed);
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_SESSION_STOP_MSG, V2G_PAYMENT_DETAILS_MSG, false),
              Reason::AuthorizationFailed);
}

TEST(HlcSessionFailedReasonTest, session_stop_before_energy_transfer_names_the_phase_the_ev_left) {
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_SESSION_STOP_MSG, V2G_CHARGE_PARAMETER_DISCOVERY_MSG, false),
              Reason::ChargingParametersNotAccepted);
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_SESSION_STOP_MSG, V2G_CABLE_CHECK_MSG, false),
              Reason::EnergyTransferSetupFailed);
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_SESSION_STOP_MSG, V2G_PRE_CHARGE_MSG, false),
              Reason::EnergyTransferSetupFailed);
}

TEST(HlcSessionFailedReasonTest, session_stop_after_power_delivery_or_welding_detection_is_a_regular_end) {
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_SESSION_STOP_MSG, V2G_POWER_DELIVERY_MSG, false),
              std::nullopt);
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_SESSION_STOP_MSG, V2G_WELDING_DETECTION_MSG, false),
              std::nullopt);
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_SESSION_STOP_MSG, V2G_UNKNOWN_MSG, false), std::nullopt);
}

TEST(HlcSessionFailedReasonTest, connection_closed_without_session_stop_maps_the_last_message) {
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_AUTHORIZATION_MSG, V2G_PAYMENT_DETAILS_MSG, false),
              Reason::AuthorizationFailed);
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_CURRENT_DEMAND_MSG, V2G_CURRENT_DEMAND_MSG, false),
              Reason::ChargingInterrupted);
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_WELDING_DETECTION_MSG, V2G_POWER_DELIVERY_MSG, false),
              std::nullopt);
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_UNKNOWN_MSG, V2G_UNKNOWN_MSG, false),
              Reason::UnexpectedSessionEnd);
}

TEST(HlcSessionFailedReasonTest, failed_tls_handshake_wins_over_the_message_history) {
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_SESSION_STOP_MSG, V2G_POWER_DELIVERY_MSG, true),
              Reason::FailedTLSHandshake);
}

} // namespace
