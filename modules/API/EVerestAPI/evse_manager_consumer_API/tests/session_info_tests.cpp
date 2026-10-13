// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <optional>
#include <utils/date.hpp>

#include "session_info.hpp"

namespace {

using module::SessionInfo;
using ApiSessionInfo = everest::lib::API::V1_0::types::evse_manager::SessionInfo;
using everest::lib::API::V1_0::types::evse_manager::EvseStateEnum;
using types::evse_manager::SessionEventEnum;

class SessionInfoReservationTest : public ::testing::Test {
protected:
    SessionInfo session_info;
    std::optional<ApiSessionInfo> last_published;

    void SetUp() override {
        session_info.set_publish_callback([this](ApiSessionInfo published) { last_published = published; });
        send(SessionEventEnum::Enabled);
    }

    void send(SessionEventEnum event) {
        types::evse_manager::SessionEvent session_event;
        session_event.uuid = "session";
        session_event.timestamp = Everest::Date::to_rfc3339(date::utc_clock::now());
        session_event.event = event;
        session_info.update_state(session_event);
    }

    const ApiSessionInfo& published() const {
        EXPECT_TRUE(last_published.has_value());
        return last_published.value();
    }
};

TEST_F(SessionInfoReservationTest, not_reserved_initially) {
    EXPECT_FALSE(published().reserved);
    EXPECT_EQ(published().state, EvseStateEnum::Unplugged);
}

TEST_F(SessionInfoReservationTest, reservation_start_and_end_toggle_reserved_without_changing_state) {
    send(SessionEventEnum::ReservationStart);
    EXPECT_TRUE(published().reserved);
    EXPECT_EQ(published().state, EvseStateEnum::Unplugged);

    send(SessionEventEnum::ReservationEnd);
    EXPECT_FALSE(published().reserved);
    EXPECT_EQ(published().state, EvseStateEnum::Unplugged);
}

TEST_F(SessionInfoReservationTest, stays_reserved_while_non_matching_ev_is_plugged_in) {
    send(SessionEventEnum::ReservationStart);
    send(SessionEventEnum::SessionStarted);
    send(SessionEventEnum::AuthRequired);

    EXPECT_TRUE(published().reserved);
    EXPECT_EQ(published().state, EvseStateEnum::AuthRequired);
}

TEST_F(SessionInfoReservationTest, transaction_started_consumes_reservation) {
    send(SessionEventEnum::ReservationStart);
    send(SessionEventEnum::SessionStarted);
    send(SessionEventEnum::TransactionStarted);

    EXPECT_FALSE(published().reserved);
}

TEST_F(SessionInfoReservationTest, reservation_end_after_disable_keeps_disabled_state) {
    send(SessionEventEnum::ReservationStart);
    send(SessionEventEnum::Disabled);
    send(SessionEventEnum::ReservationEnd);

    EXPECT_FALSE(published().reserved);
    EXPECT_EQ(published().state, EvseStateEnum::Disabled);
}

TEST_F(SessionInfoReservationTest, repeated_reservation_end_is_harmless) {
    send(SessionEventEnum::ReservationEnd);
    EXPECT_FALSE(published().reserved);

    send(SessionEventEnum::ReservationStart);
    send(SessionEventEnum::ReservationStart);
    EXPECT_TRUE(published().reserved);
}

} // namespace
