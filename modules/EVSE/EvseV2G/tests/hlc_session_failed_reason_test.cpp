// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <hlc_session_failed_reason.hpp>

#include <gtest/gtest.h>

namespace {

using Reason = types::evse_manager::HlcSessionFailedReasonEnum;

std::optional<Reason> reason_for_session_stop_after(V2gMsgTypeId msg) {
    return hlc_session_failed_reason_at_disconnect(V2G_SESSION_STOP_MSG, msg, false);
}

TEST(HlcSessionFailedReasonTest, session_stop_before_charge_parameters_names_the_phase_the_ev_left) {
    EXPECT_EQ(reason_for_session_stop_after(V2G_SESSION_SETUP_MSG), Reason::ProtocolNegotiationFailed);
    EXPECT_EQ(reason_for_session_stop_after(V2G_PAYMENT_DETAILS_MSG), Reason::AuthorizationFailed);
    EXPECT_EQ(reason_for_session_stop_after(V2G_AUTHORIZATION_MSG), Reason::AuthorizationFailed);
}

TEST(HlcSessionFailedReasonTest, session_stop_during_cable_check_or_pre_charge_is_a_setup_failure) {
    EXPECT_EQ(reason_for_session_stop_after(V2G_CABLE_CHECK_MSG), Reason::EnergyTransferSetupFailed);
    EXPECT_EQ(reason_for_session_stop_after(V2G_PRE_CHARGE_MSG), Reason::EnergyTransferSetupFailed);
}

// The preceding message alone cannot tell an EV that gave up here from a regular end.
TEST(HlcSessionFailedReasonTest, session_stop_during_parameter_negotiation_is_not_reported) {
    EXPECT_EQ(reason_for_session_stop_after(V2G_SERVICE_DISCOVERY_MSG), std::nullopt);
    EXPECT_EQ(reason_for_session_stop_after(V2G_CHARGE_PARAMETER_DISCOVERY_MSG), std::nullopt);
}

TEST(HlcSessionFailedReasonTest, session_stop_after_charging_started_is_a_regular_end) {
    EXPECT_EQ(reason_for_session_stop_after(V2G_POWER_DELIVERY_MSG), std::nullopt);
    EXPECT_EQ(reason_for_session_stop_after(V2G_CURRENT_DEMAND_MSG), std::nullopt);
    EXPECT_EQ(reason_for_session_stop_after(V2G_CHARGING_STATUS_MSG), std::nullopt);
    EXPECT_EQ(reason_for_session_stop_after(V2G_WELDING_DETECTION_MSG), std::nullopt);
}

TEST(HlcSessionFailedReasonTest, session_stop_without_a_preceding_message_is_not_reported) {
    EXPECT_EQ(reason_for_session_stop_after(V2G_UNKNOWN_MSG), std::nullopt);
}

TEST(HlcSessionFailedReasonTest, connection_closed_without_session_stop_maps_the_last_message) {
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_AUTHORIZATION_MSG, V2G_UNKNOWN_MSG, false),
              Reason::AuthorizationFailed);
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_CHARGE_PARAMETER_DISCOVERY_MSG, V2G_UNKNOWN_MSG, false),
              Reason::ChargingParametersNotAccepted);
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_POWER_DELIVERY_MSG, V2G_UNKNOWN_MSG, false),
              Reason::EnergyTransferSetupFailed);
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_CURRENT_DEMAND_MSG, V2G_UNKNOWN_MSG, false),
              Reason::ChargingInterrupted);
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_WELDING_DETECTION_MSG, V2G_UNKNOWN_MSG, false), std::nullopt);
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_UNKNOWN_MSG, V2G_UNKNOWN_MSG, false),
              Reason::UnexpectedSessionEnd);
}

TEST(HlcSessionFailedReasonTest, failed_tls_handshake_wins_over_the_message_history) {
    EXPECT_EQ(hlc_session_failed_reason_at_disconnect(V2G_SESSION_STOP_MSG, V2G_POWER_DELIVERY_MSG, true),
              Reason::FailedTLSHandshake);
}

} // namespace
