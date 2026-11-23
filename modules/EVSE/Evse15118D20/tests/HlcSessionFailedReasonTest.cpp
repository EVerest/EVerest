// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "utils.hpp"

namespace {

using module::charger::hlc_failed_reason_at_teardown;
using module::charger::HlcMessageHistory;
using Reason = types::evse_manager::HlcSessionFailedReasonEnum;
using iso15118::V2gMessageType;
using Type20 = iso15118::message_20::Type;
using Type2 = iso15118::message_2::Type;
using TypeDin = iso15118::message_din::Type;

template <typename Type> std::optional<Reason> reason_for_session_stop_after(Type type) {
    return hlc_failed_reason_at_teardown(V2gMessageType{Type::SessionStopRes}, V2gMessageType{type});
}

template <typename Type> std::optional<Reason> reason_for_close_after(Type type) {
    return hlc_failed_reason_at_teardown(V2gMessageType{type}, V2gMessageType{type});
}

TEST(HlcSessionFailedReasonTest, session_stop_before_charge_parameters_names_the_phase_the_ev_left) {
    EXPECT_EQ(reason_for_session_stop_after(Type2::SessionSetupRes), Reason::ProtocolNegotiationFailed);
    EXPECT_EQ(reason_for_session_stop_after(Type2::AuthorizationRes), Reason::AuthorizationFailed);
    EXPECT_EQ(reason_for_session_stop_after(Type20::AuthorizationRes), Reason::AuthorizationFailed);
    EXPECT_EQ(reason_for_session_stop_after(TypeDin::ContractAuthenticationRes), Reason::AuthorizationFailed);
}

TEST(HlcSessionFailedReasonTest, session_stop_request_rejected_as_out_of_sequence_is_classified_alike) {
    EXPECT_EQ(
        hlc_failed_reason_at_teardown(V2gMessageType{Type2::SessionStopReq}, V2gMessageType{Type2::AuthorizationRes}),
        Reason::AuthorizationFailed);
}

TEST(HlcSessionFailedReasonTest, session_stop_during_cable_check_or_pre_charge_is_a_setup_failure) {
    EXPECT_EQ(reason_for_session_stop_after(Type2::CableCheckRes), Reason::EnergyTransferSetupFailed);
    EXPECT_EQ(reason_for_session_stop_after(Type20::DC_CableCheckRes), Reason::EnergyTransferSetupFailed);
    EXPECT_EQ(reason_for_session_stop_after(TypeDin::PreChargeRes), Reason::EnergyTransferSetupFailed);
}

// The preceding message alone cannot tell an EV that gave up here from a regular end.
TEST(HlcSessionFailedReasonTest, session_stop_during_parameter_negotiation_is_not_reported) {
    EXPECT_EQ(reason_for_session_stop_after(Type2::ServiceDiscoveryRes), std::nullopt);
    EXPECT_EQ(reason_for_session_stop_after(Type2::ChargeParameterDiscoveryRes), std::nullopt);
    EXPECT_EQ(reason_for_session_stop_after(TypeDin::ChargeParameterDiscoveryRes), std::nullopt);
    EXPECT_EQ(reason_for_session_stop_after(Type20::ScheduleExchangeRes), std::nullopt);
}

TEST(HlcSessionFailedReasonTest, session_stop_after_charging_started_is_a_regular_end) {
    EXPECT_EQ(reason_for_session_stop_after(Type2::PowerDeliveryRes), std::nullopt);
    EXPECT_EQ(reason_for_session_stop_after(Type20::PowerDeliveryRes), std::nullopt);
    EXPECT_EQ(reason_for_session_stop_after(TypeDin::PowerDeliveryRes), std::nullopt);
    EXPECT_EQ(reason_for_session_stop_after(Type2::CurrentDemandRes), std::nullopt);
    EXPECT_EQ(reason_for_session_stop_after(Type20::DC_ChargeLoopRes), std::nullopt);
    EXPECT_EQ(reason_for_session_stop_after(Type2::WeldingDetectionRes), std::nullopt);
    EXPECT_EQ(reason_for_session_stop_after(Type20::DC_WeldingDetectionRes), std::nullopt);
}

TEST(HlcSessionFailedReasonTest, session_stop_without_a_preceding_message_is_not_reported) {
    EXPECT_EQ(hlc_failed_reason_at_teardown(V2gMessageType{Type2::SessionStopRes}, std::nullopt), std::nullopt);
}

TEST(HlcSessionFailedReasonTest, connection_closed_without_session_stop_maps_the_last_message) {
    EXPECT_EQ(reason_for_close_after(Type2::AuthorizationRes), Reason::AuthorizationFailed);
    EXPECT_EQ(reason_for_close_after(Type2::ChargeParameterDiscoveryRes), Reason::ChargingParametersNotAccepted);
    EXPECT_EQ(reason_for_close_after(Type2::PowerDeliveryRes), Reason::EnergyTransferSetupFailed);
    EXPECT_EQ(reason_for_close_after(Type20::DC_ChargeLoopRes), Reason::ChargingInterrupted);
    EXPECT_EQ(reason_for_close_after(Type2::WeldingDetectionRes), std::nullopt);
    EXPECT_EQ(hlc_failed_reason_at_teardown(std::nullopt, std::nullopt), std::nullopt);
}

std::optional<Reason> reason_at_teardown(const HlcMessageHistory& history) {
    return hlc_failed_reason_at_teardown(history.last_message(), history.last_message_before_session_stop());
}

// An EV that gives up while authorizing may start its stop sequence with PowerDeliveryReq.
TEST(HlcSessionFailedReasonTest, out_of_sequence_request_that_closes_the_connection_keeps_the_phase) {
    HlcMessageHistory history;
    history.record(TypeDin::ContractAuthenticationReq);
    history.record(TypeDin::ContractAuthenticationRes);
    history.record(TypeDin::PowerDeliveryReq);
    history.reject_last_request();
    history.record(TypeDin::PowerDeliveryRes);

    EXPECT_EQ(history.last_message(), V2gMessageType{TypeDin::ContractAuthenticationRes});
    EXPECT_EQ(reason_at_teardown(history), Reason::AuthorizationFailed);
}

// The ISO 15118-2 SECC answers a SessionStopReq with a sequence error in most states [V2G2-538].
TEST(HlcSessionFailedReasonTest, out_of_sequence_session_stop_is_classified_as_a_session_stop) {
    HlcMessageHistory authorizing;
    authorizing.record(Type2::AuthorizationReq);
    authorizing.record(Type2::AuthorizationRes);
    authorizing.record(Type2::SessionStopReq);
    authorizing.reject_last_request();
    authorizing.record(Type2::SessionStopRes);

    EXPECT_EQ(reason_at_teardown(authorizing), Reason::AuthorizationFailed);

    HlcMessageHistory negotiating;
    negotiating.record(Type2::ChargeParameterDiscoveryReq);
    negotiating.record(Type2::ChargeParameterDiscoveryRes);
    negotiating.record(Type2::SessionStopReq);
    negotiating.reject_last_request();
    negotiating.record(Type2::SessionStopRes);

    EXPECT_EQ(reason_at_teardown(negotiating), std::nullopt);

    HlcMessageHistory charging;
    charging.record(Type2::CurrentDemandReq);
    charging.record(Type2::CurrentDemandRes);
    charging.record(Type2::SessionStopReq);
    charging.reject_last_request();
    charging.record(Type2::SessionStopRes);

    EXPECT_EQ(reason_at_teardown(charging), std::nullopt);
}

TEST(HlcSessionFailedReasonTest, nothing_is_recorded_after_an_out_of_sequence_request) {
    HlcMessageHistory history;
    history.record(TypeDin::ContractAuthenticationReq);
    history.record(TypeDin::ContractAuthenticationRes);
    history.record(TypeDin::PowerDeliveryReq);
    history.reject_last_request();
    history.record(TypeDin::CurrentDemandReq);

    EXPECT_EQ(history.last_message(), V2gMessageType{TypeDin::ContractAuthenticationRes});
    EXPECT_EQ(reason_at_teardown(history), Reason::AuthorizationFailed);
}

TEST(HlcSessionFailedReasonTest, out_of_sequence_first_request_leaves_no_history) {
    HlcMessageHistory history;
    history.record(TypeDin::CurrentDemandReq);
    history.reject_last_request();
    history.record(TypeDin::CurrentDemandRes);

    EXPECT_FALSE(history.last_message().has_value());
    EXPECT_EQ(reason_at_teardown(history), std::nullopt);
}

TEST(HlcSessionFailedReasonTest, reset_history_records_again_after_an_out_of_sequence_request) {
    HlcMessageHistory history;
    history.record(Type2::PowerDeliveryReq);
    history.reject_last_request();
    history.reset();

    history.record(Type2::SessionSetupReq);
    EXPECT_EQ(history.last_message(), V2gMessageType{Type2::SessionSetupReq});
}

} // namespace
