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

TEST_F(ChargePointStateMachineTest, HandleErrorCleared__ReportsResolvedErrorInfo) {
    create_state_machine(true);
    ErrorInfo error_info_1("uuid1", ChargePointErrorCode::ConnectorLockFailure, true, "Info1", "VendorA", "VE1");
    ErrorInfo error_info_2("uuid2", ChargePointErrorCode::GroundFailure, true, "Info2", "VendorB", "VE2");

    InSequence seq;
    EXPECT_CALL(mock_callback,
                Call(FSMState::Faulted, ChargePointErrorCode::ConnectorLockFailure, _,
                     std::optional<ocpp::CiString<50>>("Info1"), std::optional<ocpp::CiString<255>>("VendorA"),
                     std::optional<ocpp::CiString<50>>("VE1")));
    EXPECT_CALL(mock_callback,
                Call(FSMState::Faulted, ChargePointErrorCode::GroundFailure, _,
                     std::optional<ocpp::CiString<50>>("Info2"), std::optional<ocpp::CiString<255>>("VendorB"),
                     std::optional<ocpp::CiString<50>>("VE2")));
    // clearing uuid1 reports it as resolved (info/vendor fields of the cleared error), while the error code still
    // reflects the remaining active error
    EXPECT_CALL(mock_callback,
                Call(FSMState::Faulted, ChargePointErrorCode::GroundFailure, _,
                     std::optional<ocpp::CiString<50>>("VE1 resolved"), std::optional<ocpp::CiString<255>>("VendorA"),
                     std::optional<ocpp::CiString<50>>("VE1")));
    // clearing uuid2 reports it as resolved and the connector leaves Faulted
    EXPECT_CALL(mock_callback,
                Call(FSMState::Available, ChargePointErrorCode::NoError, _,
                     std::optional<ocpp::CiString<50>>("VE2 resolved"), std::optional<ocpp::CiString<255>>("VendorB"),
                     std::optional<ocpp::CiString<50>>("VE2")));

    state_machine->handle_error(error_info_1);
    state_machine->handle_error(error_info_2);
    state_machine->handle_error_cleared(error_info_1.uuid);
    state_machine->handle_error_cleared(error_info_2.uuid);
}

TEST_F(ChargePointStateMachineTest, HandleErrorCleared__ResolvedErrorInfoTruncated) {
    create_state_machine(true);
    const std::string long_vendor_error_code(50, 'X');
    ErrorInfo error_info("uuid1", ChargePointErrorCode::ConnectorLockFailure, true, std::nullopt, "VendorA",
                         long_vendor_error_code);

    InSequence seq;
    EXPECT_CALL(mock_callback, Call(FSMState::Faulted, ChargePointErrorCode::ConnectorLockFailure, _, _, _, _));
    // "<long_vendor_error_code> resolved" exceeds the 50 character limit of the info field and is truncated
    EXPECT_CALL(mock_callback, Call(FSMState::Available, ChargePointErrorCode::NoError, _,
                                    std::optional<ocpp::CiString<50>>(long_vendor_error_code),
                                    std::optional<ocpp::CiString<255>>("VendorA"),
                                    std::optional<ocpp::CiString<50>>(long_vendor_error_code)));

    state_machine->handle_error(error_info);
    state_machine->handle_error_cleared(error_info.uuid);
}

TEST_F(ChargePointStateMachineTest, SuspendedEVSE__ReasonChangeEmits) {
    create_state_machine(false);
    const std::optional<ocpp::CiString<50>> first_reason("NoEnergy");
    const std::optional<ocpp::CiString<50>> second_reason("Error,NoEnergy");

    EXPECT_CALL(mock_callback, Call(_, _, _, _, _, _)).Times(0);
    {
        InSequence seq;
        EXPECT_CALL(mock_callback, Call(FSMState::SuspendedEVSE, ChargePointErrorCode::NoError, _, first_reason, _, _));
        EXPECT_CALL(mock_callback,
                    Call(FSMState::SuspendedEVSE, ChargePointErrorCode::NoError, _, second_reason, _, _));
    }

    EXPECT_TRUE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), first_reason));
    EXPECT_TRUE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), second_reason));
    EXPECT_EQ(state_machine->get_state(), FSMState::SuspendedEVSE);
}

TEST_F(ChargePointStateMachineTest, SuspendedEVSE__IdenticalReasonSuppressed) {
    create_state_machine(false);
    const std::optional<ocpp::CiString<50>> reason("NoEnergy");

    EXPECT_CALL(mock_callback, Call(_, _, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_callback, Call(FSMState::SuspendedEVSE, ChargePointErrorCode::NoError, _, reason, _, _)).Times(1);

    EXPECT_TRUE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), reason));
    EXPECT_FALSE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), reason));
}

TEST_F(ChargePointStateMachineTest, SuspendedEVSE__NulloptRepeatSuppressed) {
    create_state_machine(false);

    EXPECT_CALL(mock_callback, Call(_, _, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_callback, Call(FSMState::SuspendedEVSE, ChargePointErrorCode::NoError, _,
                                    std::optional<ocpp::CiString<50>>(), _, _))
        .Times(1);

    EXPECT_TRUE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), std::nullopt));
    EXPECT_FALSE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), std::nullopt));
}

TEST_F(ChargePointStateMachineTest, SuspendedEVSE__InterleavedErrorThenIdenticalReasonEmits) {
    create_state_machine(false);
    const std::optional<ocpp::CiString<50>> reason("NoEnergy");
    const ErrorInfo error_info("uuid1", ChargePointErrorCode::OtherError, false, "SomeError");

    EXPECT_CALL(mock_callback, Call(_, _, _, _, _, _)).Times(0);
    {
        InSequence seq;
        EXPECT_CALL(mock_callback, Call(FSMState::SuspendedEVSE, ChargePointErrorCode::NoError, _, reason, _, _));
        EXPECT_CALL(mock_callback, Call(FSMState::SuspendedEVSE, ChargePointErrorCode::OtherError, _,
                                        std::optional<ocpp::CiString<50>>("SomeError"), _, _));
        EXPECT_CALL(mock_callback, Call(FSMState::SuspendedEVSE, ChargePointErrorCode::OtherError, _, reason, _, _));
    }

    EXPECT_TRUE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), reason));
    EXPECT_TRUE(state_machine->handle_error(error_info));
    EXPECT_TRUE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), reason));
}

// A connector that leaves SuspendedEVSE and is suspended again for the same reason must report the re-entry. The
// self-transition guard must stay narrow enough to let this through.
TEST_F(ChargePointStateMachineTest, SuspendedEVSE__SameReasonAfterChargingEmits) {
    create_state_machine(false);
    const std::optional<ocpp::CiString<50>> reason("NoEnergy");

    EXPECT_CALL(mock_callback, Call(_, _, _, _, _, _)).Times(0);
    {
        InSequence seq;
        EXPECT_CALL(mock_callback, Call(FSMState::SuspendedEVSE, ChargePointErrorCode::NoError, _, reason, _, _));
        EXPECT_CALL(mock_callback, Call(FSMState::Charging, ChargePointErrorCode::NoError, _,
                                        std::optional<ocpp::CiString<50>>(), _, _));
        EXPECT_CALL(mock_callback, Call(FSMState::SuspendedEVSE, ChargePointErrorCode::NoError, _, reason, _, _));
    }

    EXPECT_TRUE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), reason));
    EXPECT_TRUE(state_machine->handle_event(FSMEvent::StartCharging, ocpp::DateTime(), std::nullopt));
    EXPECT_TRUE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), reason));
    EXPECT_EQ(state_machine->get_state(), FSMState::SuspendedEVSE);
    EXPECT_EQ(state_machine->get_suspend_reason(), reason);
}

TEST_F(ChargePointStateMachineTest, Finishing__PauseChargingEVSERejected) {
    create_state_machine(false);
    const std::optional<ocpp::CiString<50>> reason("NoEnergy");

    EXPECT_CALL(mock_callback, Call(_, _, _, _, _, _)).Times(0);
    {
        InSequence seq;
        EXPECT_CALL(mock_callback, Call(FSMState::SuspendedEVSE, ChargePointErrorCode::NoError, _, reason, _, _));
        EXPECT_CALL(mock_callback, Call(FSMState::Finishing, ChargePointErrorCode::NoError, _, _, _, _));
    }

    EXPECT_TRUE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), reason));
    EXPECT_TRUE(
        state_machine->handle_event(FSMEvent::TransactionStoppedAndUserActionRequired, ocpp::DateTime(), std::nullopt));
    EXPECT_FALSE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), reason));
    EXPECT_EQ(state_machine->get_state(), FSMState::Finishing);
}

TEST_F(ChargePointStateMachineTest, SuspendedEVSE__NoEmitWhileFaulted) {
    create_state_machine(false);
    const std::optional<ocpp::CiString<50>> reason("NoEnergy");
    const std::optional<ocpp::CiString<50>> changed_reason("Error,NoEnergy");
    const ErrorInfo fault("uuid1", ChargePointErrorCode::GroundFailure, true);

    EXPECT_CALL(mock_callback, Call(_, _, _, _, _, _)).Times(0);
    {
        InSequence seq;
        EXPECT_CALL(mock_callback, Call(FSMState::SuspendedEVSE, ChargePointErrorCode::NoError, _, reason, _, _));
        EXPECT_CALL(mock_callback, Call(FSMState::Faulted, ChargePointErrorCode::GroundFailure, _, _, _, _));
    }

    EXPECT_TRUE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), reason));
    EXPECT_TRUE(state_machine->handle_error(fault));
    EXPECT_TRUE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), changed_reason));
    EXPECT_EQ(state_machine->get_state(), FSMState::Faulted);
}

// ChargePointFSM::trigger_status_notification drives the per-connector StatusNotification.req burst that follows an
// accepted BootNotification and a reconnect. It reports error state only: without an active error the burst carries no
// info, whatever info the last transition happened to carry.
TEST_F(ChargePointStateMachineTest, StatusNotificationBurst__NoInfoWithoutActiveError) {
    create_state_machine(false);
    const std::optional<ocpp::CiString<50>> reason("NoEnergy");

    EXPECT_CALL(mock_callback, Call(_, _, _, _, _, _)).Times(0);
    {
        InSequence seq;
        EXPECT_CALL(mock_callback, Call(FSMState::SuspendedEVSE, ChargePointErrorCode::NoError, _, reason, _, _));
        EXPECT_CALL(mock_callback, Call(FSMState::SuspendedEVSE, ChargePointErrorCode::NoError, _,
                                        std::optional<ocpp::CiString<50>>(), _, _));
    }

    EXPECT_TRUE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), reason));
    state_machine->trigger_status_notification();
}

TEST_F(ChargePointStateMachineTest, StatusNotificationBurst__ReportsActiveErrorInfo) {
    create_state_machine(false);
    const std::optional<ocpp::CiString<50>> reason("NoEnergy");
    const ErrorInfo error_info("uuid1", ChargePointErrorCode::OtherError, false, "SomeError");

    EXPECT_CALL(mock_callback, Call(_, _, _, _, _, _)).Times(0);
    {
        InSequence seq;
        EXPECT_CALL(mock_callback, Call(FSMState::SuspendedEVSE, ChargePointErrorCode::NoError, _, reason, _, _));
        EXPECT_CALL(mock_callback, Call(FSMState::SuspendedEVSE, ChargePointErrorCode::OtherError, _,
                                        std::optional<ocpp::CiString<50>>("SomeError"), _, _))
            .Times(2);
    }

    EXPECT_TRUE(state_machine->handle_event(FSMEvent::PauseChargingEVSE, ocpp::DateTime(), reason));
    EXPECT_TRUE(state_machine->handle_error(error_info));
    state_machine->trigger_status_notification();
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
