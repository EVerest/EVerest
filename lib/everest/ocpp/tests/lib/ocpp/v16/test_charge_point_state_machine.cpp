// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2024 Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <ocpp/v16/charge_point_state_machine.hpp>
#include <ocpp/v16/ocpp_enums.hpp>
#include <ocpp/v16/ocpp_types.hpp>

using namespace ocpp::v16;
using ::testing::_;
using ::testing::InSequence;

class MockStatusNotificationCallback {
public:
    MOCK_METHOD(void, Call,
                (FSMState state, ChargePointErrorCode error_code, ocpp::DateTime timestamp,
                 std::optional<ocpp::CiString<50>> info, std::optional<ocpp::CiString<255>> vendor_id,
                 std::optional<ocpp::CiString<50>> vendor_error_code));
};

class ChargePointStateMachineTest : public ::testing::Test {
protected:
    void SetUp() override {
        status_notification_callback = [&](FSMState state, ChargePointErrorCode error_code, ocpp::DateTime timestamp,
                                           std::optional<ocpp::CiString<50>> info,
                                           std::optional<ocpp::CiString<255>> vendor_id,
                                           std::optional<ocpp::CiString<50>> vendor_error_code) {
            mock_callback.Call(state, error_code, timestamp, info, vendor_id, vendor_error_code);
        };
    }

    void create_state_machine(bool report_cleared_errors) {
        state_machine =
            std::make_unique<ChargePointFSM>(status_notification_callback, FSMState::Available, report_cleared_errors);
    }

    std::unique_ptr<ChargePointFSM> state_machine;
    MockStatusNotificationCallback mock_callback;
    std::function<void(FSMState, ChargePointErrorCode, ocpp::DateTime, std::optional<ocpp::CiString<50>>,
                       std::optional<ocpp::CiString<255>>, std::optional<ocpp::CiString<50>>)>
        status_notification_callback;
};

TEST_F(ChargePointStateMachineTest, HandleError) {
    create_state_machine(false);
    ErrorInfo error_info_1("uuid1", ChargePointErrorCode::ConnectorLockFailure, true);
    ErrorInfo error_info_2("uuid2", ChargePointErrorCode::GroundFailure, true);
    EXPECT_CALL(mock_callback, Call(FSMState::Faulted, ChargePointErrorCode::ConnectorLockFailure, _, _, _, _))
        .Times(1);
    EXPECT_CALL(mock_callback, Call(FSMState::Faulted, ChargePointErrorCode::GroundFailure, _, _, _, _)).Times(1);

    state_machine->handle_error(error_info_1);
    state_machine->handle_error(error_info_2);
}

TEST_F(ChargePointStateMachineTest, HandleError__ChangeState) {
    create_state_machine(false);
    ErrorInfo error_info_1("uuid1", ChargePointErrorCode::GroundFailure, false, "InfoField", "vendor_id");
    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::GroundFailure, _, _, _, _)).Times(1);
    EXPECT_CALL(mock_callback, Call(FSMState::Preparing, ChargePointErrorCode::GroundFailure, _, _, _, _)).Times(1);

    state_machine->handle_error(error_info_1);
    state_machine->handle_event(FSMEvent::UsageInitiated, ocpp::DateTime(), "AnotherInfoField");
}

TEST_F(ChargePointStateMachineTest, HandleErrorCleared) {
    create_state_machine(false);
    ErrorInfo error_info("uuid1", ChargePointErrorCode::ConnectorLockFailure, true);
    state_machine->handle_error(error_info);

    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::NoError, _, _, _, _)).Times(1);

    state_machine->handle_error_cleared("uuid1");
}

TEST_F(ChargePointStateMachineTest, HandleErrorCleared__TwoErrors__OneCleared) {
    create_state_machine(false);
    ErrorInfo error_info_1("uuid1", ChargePointErrorCode::ConnectorLockFailure, false);
    ErrorInfo error_info_2("uuid2", ChargePointErrorCode::GroundFailure, false);

    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::ConnectorLockFailure, _, _, _, _))
        .Times(2);
    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::GroundFailure, _, _, _, _)).Times(1);
    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::NoError, _, _, _, _)).Times(0);

    state_machine->handle_error(error_info_1);
    state_machine->handle_error(error_info_2);
    state_machine->handle_error_cleared("uuid2");

    const auto latest_error = state_machine->get_latest_error();

    EXPECT_TRUE(latest_error.has_value());
    EXPECT_EQ(latest_error.value().error_code, ChargePointErrorCode::ConnectorLockFailure);
}

TEST_F(ChargePointStateMachineTest, HandleError__NonFault) {
    create_state_machine(false);
    ErrorInfo error_info("uuid1", ChargePointErrorCode::ConnectorLockFailure, false);
    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::ConnectorLockFailure, _, _, _, _))
        .Times(1);

    state_machine->handle_error(error_info);
}

TEST_F(ChargePointStateMachineTest, HandleErrorCleared__NonFault) {
    create_state_machine(false);
    ErrorInfo error_info("uuid1", ChargePointErrorCode::ConnectorLockFailure, false);

    state_machine->handle_error(error_info);

    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::NoError, _, _, _, _)).Times(1);

    state_machine->handle_error_cleared("uuid1");
}

TEST_F(ChargePointStateMachineTest, HandleErrorCleared__ClearUnknown) {
    create_state_machine(false);
    ErrorInfo error_info("uuid1", ChargePointErrorCode::ConnectorLockFailure, false);

    state_machine->handle_error(error_info);

    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::NoError, _, _, _, _)).Times(0);

    state_machine->handle_error_cleared("uuid2");
    state_machine->handle_error_cleared("uuid3");
    state_machine->handle_error_cleared("uuid4");
}

TEST_F(ChargePointStateMachineTest, HandleErrorCleared__NonFault__StillActive) {
    create_state_machine(false);
    ErrorInfo error_info_1("uuid1", ChargePointErrorCode::ConnectorLockFailure, false);
    ErrorInfo error_info_2("uuid2", ChargePointErrorCode::GroundFailure, true);

    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::ConnectorLockFailure, _, _, _, _))
        .Times(2);
    EXPECT_CALL(mock_callback, Call(FSMState::Faulted, ChargePointErrorCode::GroundFailure, _, _, _, _)).Times(1);
    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::NoError, _, _, _, _)).Times(1);

    state_machine->handle_error(error_info_1);
    state_machine->handle_error(error_info_2);
    state_machine->handle_error_cleared("uuid2");

    const auto latest_error = state_machine->get_latest_error();

    EXPECT_TRUE(latest_error.has_value());
    EXPECT_EQ(latest_error.value().error_code, ChargePointErrorCode::ConnectorLockFailure);

    state_machine->handle_error_cleared("uuid1");
}

TEST_F(ChargePointStateMachineTest, HandleErrorCleared__MultipleFaults__OutOfOrder) {
    create_state_machine(true);
    ErrorInfo error_info_1("uuid1", ChargePointErrorCode::ConnectorLockFailure, false);
    ErrorInfo error_info_2("uuid2", ChargePointErrorCode::GroundFailure, true);
    ErrorInfo error_info_3("uuid3", ChargePointErrorCode::EVCommunicationError, false);

    InSequence seq;
    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::ConnectorLockFailure, _, _, _, _));
    EXPECT_CALL(mock_callback, Call(FSMState::Faulted, ChargePointErrorCode::GroundFailure, _, _, _, _));
    EXPECT_CALL(mock_callback, Call(FSMState::Faulted, ChargePointErrorCode::EVCommunicationError, _, _, _, _));
    EXPECT_CALL(mock_callback, Call(FSMState::Faulted, ChargePointErrorCode::EVCommunicationError, _, _, _, _));
    EXPECT_CALL(mock_callback, Call(FSMState::Faulted, ChargePointErrorCode::GroundFailure, _, _, _, _));
    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::NoError, _, _, _, _));

    state_machine->handle_error(error_info_1);
    state_machine->handle_error(error_info_2);
    state_machine->handle_error(error_info_3);
    state_machine->handle_error_cleared(error_info_1.uuid);
    state_machine->handle_error_cleared(error_info_3.uuid);
    state_machine->handle_error_cleared(error_info_2.uuid);
}

TEST_F(ChargePointStateMachineTest, HandleErrorCleared__MultipleFaults__OutOfOrder__Disabled) {
    create_state_machine(false);
    ErrorInfo error_info_1("uuid1", ChargePointErrorCode::ConnectorLockFailure, false);
    ErrorInfo error_info_2("uuid2", ChargePointErrorCode::GroundFailure, true);
    ErrorInfo error_info_3("uuid3", ChargePointErrorCode::EVCommunicationError, false);

    InSequence seq;
    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::ConnectorLockFailure, _, _, _, _));
    EXPECT_CALL(mock_callback, Call(FSMState::Faulted, ChargePointErrorCode::GroundFailure, _, _, _, _));
    EXPECT_CALL(mock_callback, Call(FSMState::Faulted, ChargePointErrorCode::EVCommunicationError, _, _, _, _));
    // no StatusNotification is expected for clearing uuid1 or uuid3, since the connector remains Faulted
    // (error_info_2 is still active) and ReportClearedErrors is disabled
    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::NoError, _, _, _, _));

    state_machine->handle_error(error_info_1);
    state_machine->handle_error(error_info_2);
    state_machine->handle_error(error_info_3);
    state_machine->handle_error_cleared(error_info_1.uuid);
    state_machine->handle_error_cleared(error_info_3.uuid);
    state_machine->handle_error_cleared(error_info_2.uuid);
}

TEST_F(ChargePointStateMachineTest, HandleErrorCleared__NonFault__StillActive__Disabled__ReportsLatestRemainingInfo) {
    create_state_machine(false);
    ErrorInfo error_info_1("uuid1", ChargePointErrorCode::ConnectorLockFailure, false, "Info1", "VendorA", "VE1");
    ErrorInfo error_info_2("uuid2", ChargePointErrorCode::GroundFailure, false, "Info2", "VendorB", "VE2");

    state_machine->handle_error(error_info_1);
    state_machine->handle_error(error_info_2);

    // clearing uuid1 must report error_info_2's info/vendor fields (the latest remaining error), not uuid1's
    EXPECT_CALL(mock_callback,
                Call(FSMState::Available, ChargePointErrorCode::GroundFailure, _,
                     std::optional<ocpp::CiString<50>>("Info2"), std::optional<ocpp::CiString<255>>("VendorB"),
                     std::optional<ocpp::CiString<50>>("VE2")))
        .Times(1);

    state_machine->handle_error_cleared(error_info_1.uuid);
}
