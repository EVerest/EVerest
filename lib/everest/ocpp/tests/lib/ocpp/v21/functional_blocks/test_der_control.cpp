// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ocpp/v21/functional_blocks/der_control.hpp>

#include <ocpp/v21/messages/NotifyDERAlarm.hpp>

#include <ocpp/common/call_types.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/ocpp_enums.hpp>
#include <ocpp/v2/ocpp_types.hpp>

#include <everest/database/sqlite/connection.hpp>

#include "component_state_manager_mock.hpp"
#include "connectivity_manager_mock.hpp"
#include "device_model_test_helper.hpp"
#include "evse_manager_fake.hpp"
#include "evse_security_mock.hpp"
#include "message_dispatcher_mock.hpp"
#include "mocks/database_handler_mock.hpp"

using namespace ocpp::v2;
using namespace ocpp::v21;
using ::testing::_;
using ::testing::ByMove;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Return;

namespace {
class TransactionMock : public everest::db::sqlite::TransactionInterface {
public:
    MOCK_METHOD(void, commit, (), (override));
    MOCK_METHOD(void, rollback, (), (override));
};
} // namespace

class DERControlTest : public ::testing::Test {
protected:
    DeviceModelTestHelper device_model_test_helper;
    MockMessageDispatcher mock_dispatcher;
    DeviceModel* device_model;
    ::testing::NiceMock<ConnectivityManagerMock> connectivity_manager;
    ::testing::NiceMock<DatabaseHandlerMock> database_handler_mock;
    ocpp::EvseSecurityMock evse_security;
    EvseManagerFake evse_manager;
    ComponentStateManagerMock component_state_manager;
    std::atomic<ocpp::OcppProtocolVersion> ocpp_version;
    FunctionalBlockContext functional_block_context;

    DERControlTest() :
        device_model_test_helper(),
        mock_dispatcher(),
        device_model(device_model_test_helper.get_device_model()),
        connectivity_manager(),
        database_handler_mock(),
        evse_security(),
        evse_manager(1),
        component_state_manager(),
        ocpp_version(ocpp::OcppProtocolVersion::v21),
        functional_block_context{
            this->mock_dispatcher,       *this->device_model, this->connectivity_manager,    this->evse_manager,
            this->database_handler_mock, this->evse_security, this->component_state_manager, this->ocpp_version} {
        // Every DB-mutating path in DERControl opens a transaction. By default hand
        // out a fresh NiceMock transaction so tests that don't care about the
        // transaction lifecycle still work; tests that DO care set their own
        // expectation with WillOnce(Return(ByMove(...))) which takes precedence.
        ON_CALL(database_handler_mock, begin_transaction()).WillByDefault(Invoke([] {
            return std::make_unique<::testing::NiceMock<TransactionMock>>();
        }));
    }

    // DCDERCtrlr values for EVSE 1 come from the test-only DER config at
    // tests/config/v2/DCDERCtrlr_1.json, imported into the example device model for the unit
    // tests only (it is intentionally not part of the shipped library config). ModesSupported
    // lists FreqDroop,FreqWatt,VoltWatt,LimitMaxDischarge,VoltVar,FixedVar,EnterService,Gradients
    // and excludes HFMustTrip (the not-supported case).

    ocpp::EnhancedMessage<MessageType> make_set_der_control_msg(const SetDERControlRequest& req) {
        ocpp::Call<SetDERControlRequest> call(req);
        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.messageType = MessageType::SetDERControl;
        enhanced_message.message = call;
        return enhanced_message;
    }

    ocpp::EnhancedMessage<MessageType> make_get_der_control_msg(const GetDERControlRequest& req) {
        ocpp::Call<GetDERControlRequest> call(req);
        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.messageType = MessageType::GetDERControl;
        enhanced_message.message = call;
        return enhanced_message;
    }

    ocpp::EnhancedMessage<MessageType> make_clear_der_control_msg(const ClearDERControlRequest& req) {
        ocpp::Call<ClearDERControlRequest> call(req);
        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.messageType = MessageType::ClearDERControl;
        enhanced_message.message = call;
        return enhanced_message;
    }

    SetDERControlRequest make_freq_droop_request(const std::string& control_id, bool is_default, int32_t priority) {
        SetDERControlRequest req;
        req.isDefault = is_default;
        req.controlId = control_id;
        req.controlType = DERControlEnum::FreqDroop;
        FreqDroop fd;
        fd.priority = priority;
        fd.overFreq = 61.0f;
        fd.underFreq = 59.0f;
        fd.overDroop = 5.0f;
        fd.underDroop = 5.0f;
        fd.responseTime = 3.0f;
        req.freqDroop = fd;
        return req;
    }

    SetDERControlRequest make_volt_watt_curve_request(const std::string& control_id, bool is_default,
                                                      int32_t priority) {
        SetDERControlRequest req;
        req.isDefault = is_default;
        req.controlId = control_id;
        req.controlType = DERControlEnum::VoltWatt;
        DERCurve curve;
        curve.priority = priority;
        curve.yUnit = DERUnitEnum::PctMaxW;
        DERCurvePoints p1;
        p1.x = 0.97f;
        p1.y = 100.0f;
        DERCurvePoints p2;
        p2.x = 1.03f;
        p2.y = 0.0f;
        curve.curveData = {p1, p2};
        req.curve = curve;
        return req;
    }
};

// --- Message dispatch tests ---

TEST_F(DERControlTest, HandleMessage_UnknownType_Throws) {
    DERControl der_control(functional_block_context);
    ocpp::EnhancedMessage<MessageType> msg;
    msg.messageType = MessageType::Authorize;
    EXPECT_THROW(der_control.handle_message(msg), MessageTypeNotImplementedException);
}

// --- R04.FR.01: Unsupported controlType returns NotSupported ---

TEST_F(DERControlTest, SetDERControl_UnsupportedType_ReturnsNotSupported) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-unsup";
    req.controlType = DERControlEnum::HFMustTrip; // Not in ModesSupported
    DERCurve curve;
    curve.priority = 0;
    curve.yUnit = DERUnitEnum::Not_Applicable;
    DERCurvePoints p1;
    p1.x = 62.0f;
    p1.y = 1.0f;
    curve.curveData = {p1};
    req.curve = curve;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotSupported);
    }));

    der_control.handle_message(msg);
}

// --- R04.FR.02: New default control accepted ---

TEST_F(DERControlTest, SetDERControl_NewDefault_Accepted) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-default-1", true, 0);
    auto msg = make_set_der_control_msg(req);

    // Expect DB query for existing controls of same type
    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(std::optional<bool>(true), std::optional<std::string>("FreqDroop"),
                                                   std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{})); // No existing

    // Expect DB insert
    EXPECT_CALL(database_handler_mock,
                insert_or_update_der_control("ctrl-default-1", true, "FreqDroop", false, 0, _, _, _))
        .Times(1);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// --- R04.FR.13: Default with startTime rejected ---

TEST_F(DERControlTest, SetDERControl_DefaultWithStartTime_Rejected) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req = make_freq_droop_request("ctrl-bad-default", true, 0);
    req.freqDroop.value().startTime = ocpp::DateTime();

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// --- R04.FR.15: Scheduled EnterService rejected ---

TEST_F(DERControlTest, SetDERControl_ScheduledEnterService_Rejected) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = false;
    req.controlId = "ctrl-enter-sched";
    req.controlType = DERControlEnum::EnterService;
    EnterService es;
    es.priority = 0;
    es.highVoltage = 264.0f;
    es.lowVoltage = 211.0f;
    es.highFreq = 62.0f;
    es.lowFreq = 58.0f;
    req.enterService = es;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// --- R04.FR.15: Scheduled Gradients rejected ---

TEST_F(DERControlTest, SetDERControl_ScheduledGradients_Rejected) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = false;
    req.controlId = "ctrl-grad-sched";
    req.controlType = DERControlEnum::Gradients;
    Gradient grad;
    grad.priority = 0;
    grad.gradient = 10.0f;
    grad.softGradient = 5.0f;
    req.gradient = grad;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// --- R04.FR.16-17: Wrong control field for type → Rejected ---

TEST_F(DERControlTest, SetDERControl_WrongControlField_Rejected) {
    DERControl der_control(functional_block_context);

    // FreqDroop type but providing curve instead of freqDroop field
    SetDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-wrong-field";
    req.controlType = DERControlEnum::FreqDroop;
    DERCurve curve;
    curve.priority = 0;
    curve.yUnit = DERUnitEnum::PctMaxW;
    DERCurvePoints p1;
    p1.x = 1.0f;
    p1.y = 100.0f;
    curve.curveData = {p1};
    req.curve = curve; // Wrong, should be freqDroop

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// --- R04.FR.16-17: Multiple control fields → Rejected ---

TEST_F(DERControlTest, SetDERControl_MultipleControlFields_Rejected) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req = make_freq_droop_request("ctrl-multi", true, 0);
    // Also set a curve, two fields populated
    DERCurve curve;
    curve.priority = 0;
    curve.yUnit = DERUnitEnum::PctMaxW;
    DERCurvePoints p1;
    p1.x = 1.0f;
    p1.y = 100.0f;
    curve.curveData = {p1};
    req.curve = curve;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// --- R04.FR.05: New scheduled control accepted ---

TEST_F(DERControlTest, SetDERControl_NewScheduled_Accepted) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-sched-1", false, 0);
    req.freqDroop.value().startTime = ocpp::DateTime();
    req.freqDroop.value().duration = 3600.0f;
    auto msg = make_set_der_control_msg(req);

    // No existing scheduled controls
    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(std::optional<bool>(false), std::optional<std::string>("FreqDroop"),
                                                   std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{}));

    EXPECT_CALL(database_handler_mock,
                insert_or_update_der_control("ctrl-sched-1", false, "FreqDroop", false, 0, _, _, _))
        .Times(1);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// --- SetDERControl supersede+insert runs inside a single DB transaction ---

TEST_F(DERControlTest, SetDERControl_SupersedeAndInsertRunInsideTransaction) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-new", false, 0);
    req.freqDroop.value().startTime = ocpp::DateTime();
    req.freqDroop.value().duration = 3600.0f;
    auto msg = make_set_der_control_msg(req);

    std::string existing =
        R"({"isSuperseded":false,"priority":5,"request":{"controlId":"ctrl-existing","controlType":"FreqDroop","isDefault":false}})";

    auto transaction = std::make_unique<TransactionMock>();
    auto* transaction_raw = transaction.get();

    InSequence seq;
    EXPECT_CALL(database_handler_mock, begin_transaction()).WillOnce(Return(ByMove(std::move(transaction))));
    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(std::optional<bool>(false), std::optional<std::string>("FreqDroop"),
                                                   std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{existing}));
    EXPECT_CALL(database_handler_mock, update_der_control_superseded("ctrl-existing", true));
    EXPECT_CALL(database_handler_mock, insert_or_update_der_control("ctrl-new", false, "FreqDroop", false, 0, _, _, _));
    // startTime <= now, so STARTED_NOTIFIED is flagged inside the same transaction
    // before commit, and the NotifyDERStartStop dispatch runs strictly after commit.
    EXPECT_CALL(database_handler_mock, mark_der_control_started_notified("ctrl-new"));
    EXPECT_CALL(*transaction_raw, commit());
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)); // NotifyDERStartStop

    der_control.handle_message(msg);
}

// --- Persistence: verify DB insert is called ---

TEST_F(DERControlTest, SetDERControl_PersistsToDatabase) {
    DERControl der_control(functional_block_context);

    auto req = make_volt_watt_curve_request("ctrl-persist", true, 0);
    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(std::optional<bool>(true), std::optional<std::string>("VoltWatt"),
                                                   std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{}));

    // The key assertion: DB insert must be called
    EXPECT_CALL(database_handler_mock,
                insert_or_update_der_control("ctrl-persist", true, "VoltWatt", false, 0, _, _, _))
        .Times(1);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// =============================================================================
// GetDERControl tests (R04.FR.30-37)
// =============================================================================

// R04.FR.30 - No matching controls → NotFound
TEST_F(DERControlTest, GetDERControl_NoControls_NotFound) {
    DERControl der_control(functional_block_context);

    GetDERControlRequest req;
    req.requestId = 1;
    req.controlType = DERControlEnum::FreqDroop;

    auto msg = make_get_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<GetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotFound);
    }));

    der_control.handle_message(msg);
}

// R04.FR.36 - Unsupported controlType → NotSupported
TEST_F(DERControlTest, GetDERControl_UnsupportedType_NotSupported) {
    DERControl der_control(functional_block_context);

    GetDERControlRequest req;
    req.requestId = 2;
    req.controlType = DERControlEnum::HFMustTrip; // Not in ModesSupported

    auto msg = make_get_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<GetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotSupported);
    }));

    der_control.handle_message(msg);
}

// R04.FR.33 - No filters → returns all, sends ReportDERControl
TEST_F(DERControlTest, GetDERControl_NoFilters_ReturnsAll) {
    DERControl der_control(functional_block_context);

    GetDERControlRequest req;
    req.requestId = 3;

    auto msg = make_get_der_control_msg(req);

    std::string control_json =
        R"({"isSuperseded":false,"priority":0,"request":{"controlId":"ctrl-1","controlType":"FreqDroop","isDefault":true}})";
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{control_json}));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<GetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    // Expect ReportDERControl to be dispatched
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(1);

    der_control.handle_message(msg);
}

// R04.FR.34 - Filter by controlType
TEST_F(DERControlTest, GetDERControl_ByType_ReportsMatching) {
    DERControl der_control(functional_block_context);

    GetDERControlRequest req;
    req.requestId = 4;
    req.controlType = DERControlEnum::FreqDroop;

    auto msg = make_get_der_control_msg(req);

    std::string control_json =
        R"({"isSuperseded":false,"priority":0,"request":{"controlId":"ctrl-fd","controlType":"FreqDroop","isDefault":true}})";
    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(_, std::optional<std::string>("FreqDroop"), _))
        .WillOnce(Return(std::vector<std::string>{control_json}));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<GetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(1);

    der_control.handle_message(msg);
}

// =============================================================================
// ClearDERControl tests (R04.FR.40-46)
// =============================================================================

// R04.FR.41 - controlType not found → NotFound
TEST_F(DERControlTest, ClearDERControl_TypeNotFound_NotFound) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = true;
    req.controlType = DERControlEnum::FreqDroop;

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock,
                delete_der_controls_matching_criteria(true, std::optional<std::string>("FreqDroop")))
        .WillOnce(Return(0));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotFound);
    }));

    der_control.handle_message(msg);
}

// R04.FR.42 - controlId not found → NotFound
TEST_F(DERControlTest, ClearDERControl_ControlIdNotFound_NotFound) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = true;
    req.controlId = "nonexistent-id";

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, delete_der_control_by_id_and_default("nonexistent-id", true))
        .WillOnce(Return(false));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotFound);
    }));

    der_control.handle_message(msg);
}

// R04.FR.42 - controlId exists but under different isDefault → NotFound.
// The delete must be scoped by BOTH controlId and isDefault so the caller
// can't accidentally remove a scheduled control by asking to clear a default
// (or vice versa).
TEST_F(DERControlTest, ClearDERControl_ControlIdWithWrongIsDefault_NotFound) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = true; // caller believes it's a default
    req.controlId = "ctrl-x";
    auto msg = make_clear_der_control_msg(req);

    // Row exists but only as a scheduled control, delete returns 0 rows
    EXPECT_CALL(database_handler_mock, delete_der_control_by_id_and_default("ctrl-x", true)).WillOnce(Return(false));
    EXPECT_CALL(database_handler_mock, delete_der_control(_)).Times(0);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotFound);
    }));

    der_control.handle_message(msg);
}

// R04.FR.43 - Unsupported controlType, no controlId → NotSupported
TEST_F(DERControlTest, ClearDERControl_UnsupportedType_NotSupported) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = true;
    req.controlType = DERControlEnum::HFMustTrip; // Not in ModesSupported

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotSupported);
    }));

    der_control.handle_message(msg);
}

// R04.FR.44 - No controlType, no controlId → clear all by isDefault
TEST_F(DERControlTest, ClearDERControl_AllByDefault_Accepted) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = true;

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock,
                delete_der_controls_matching_criteria(true, std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(3));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// R04.FR.44 — bulk clear (no controlType, no controlId) is unconditionally
// Accepted, even when there is nothing to delete. The "NotFound" status is
// scoped to FR.41/.42 (controlType-with-no-match and controlId-with-no-match).
// TC_R_102_CS step 1 hits this on a fresh CS where the DB has no DER
// controls yet.
TEST_F(DERControlTest, ClearDERControl_AllByDefault_EmptyDb_Accepted) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = true;

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock,
                delete_der_controls_matching_criteria(true, std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(0));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// R04.FR.44 — same as above for the scheduled side. TC_R_102 step 3 sends
// ClearDERControl(isDefault=false) immediately after step 1 with the DB still
// empty.
TEST_F(DERControlTest, ClearDERControl_AllByScheduled_EmptyDb_Accepted) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = false;

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock,
                delete_der_controls_matching_criteria(false, std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(0));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// R04.FR.45 - controlType set, no controlId → clear by type and isDefault
TEST_F(DERControlTest, ClearDERControl_ByType_Accepted) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = false;
    req.controlType = DERControlEnum::VoltWatt;

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock,
                delete_der_controls_matching_criteria(false, std::optional<std::string>("VoltWatt")))
        .WillOnce(Return(2));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// R04.FR.46 - controlId set → clear specific control
TEST_F(DERControlTest, ClearDERControl_ByControlId_Accepted) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-to-delete";

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, delete_der_control_by_id_and_default("ctrl-to-delete", true))
        .WillOnce(Return(true));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// =============================================================================
// NotifyDERStartStop tests (R04.FR.20-22)
// =============================================================================

// R04.FR.20 - When a scheduled control's startTime arrives, CS sends NotifyDERStartStop started=true
// We test this by sending a SetDERControl with startTime in the past → should trigger immediate start notification
TEST_F(DERControlTest, NotifyStartStop_ImmediateStart_SendsNotification) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-immediate", false, 0);
    // Set startTime to now (effectively immediate)
    req.freqDroop.value().startTime = ocpp::DateTime();
    req.freqDroop.value().duration = 3600.0f;
    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(std::optional<bool>(false), std::optional<std::string>("FreqDroop"),
                                                   std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{}));

    EXPECT_CALL(database_handler_mock,
                insert_or_update_der_control("ctrl-immediate", false, "FreqDroop", false, 0, _, _, _))
        .Times(1);

    // Expect SetDERControlResponse (Accepted)
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    // R04.FR.20: Expect NotifyDERStartStop with started=true
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool /*triggered*/) {
        // Verify it's a NotifyDERStartStop call
        auto action = call[ocpp::CALL_ACTION].get<std::string>();
        EXPECT_EQ(action, "NotifyDERStartStop");
        auto payload = call[ocpp::CALL_PAYLOAD];
        EXPECT_EQ(payload["controlId"], "ctrl-immediate");
        EXPECT_TRUE(payload["started"].get<bool>());
    }));

    der_control.handle_message(msg);
}

// R04.FR.21 - When a new control supersedes an active one, send NotifyDERStartStop with supersededIds
TEST_F(DERControlTest, NotifyStartStop_SupersedingControl_SendsSupersededIds) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-higher-prio", false, 0); // priority 0 = highest
    req.freqDroop.value().startTime = ocpp::DateTime();
    req.freqDroop.value().duration = 3600.0f;
    auto msg = make_set_der_control_msg(req);

    // Return an existing active control with lower priority (higher value)
    std::string existing =
        R"({"isSuperseded":false,"priority":5,"request":{"controlId":"ctrl-lower-prio","controlType":"FreqDroop","isDefault":false}})";
    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(std::optional<bool>(false), std::optional<std::string>("FreqDroop"),
                                                   std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{existing}));

    EXPECT_CALL(database_handler_mock, update_der_control_superseded("ctrl-lower-prio", true)).Times(1);
    EXPECT_CALL(database_handler_mock,
                insert_or_update_der_control("ctrl-higher-prio", false, "FreqDroop", false, 0, _, _, _))
        .Times(1);

    // Expect SetDERControlResponse with supersededIds
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
        ASSERT_TRUE(response.supersededIds.has_value());
        EXPECT_EQ(response.supersededIds->size(), 1);
        EXPECT_EQ(response.supersededIds->at(0).get(), "ctrl-lower-prio");
    }));

    // Expect NotifyDERStartStop with started=true and supersededIds
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool /*triggered*/) {
        auto payload = call[ocpp::CALL_PAYLOAD];
        EXPECT_EQ(payload["controlId"], "ctrl-higher-prio");
        EXPECT_TRUE(payload["started"].get<bool>());
        ASSERT_TRUE(payload.contains("supersededIds"));
        EXPECT_EQ(payload["supersededIds"].size(), 1);
        EXPECT_EQ(payload["supersededIds"][0], "ctrl-lower-prio");
    }));

    der_control.handle_message(msg);
}

// =============================================================================
// yUnit validation tests (R04.FR.50-56)
// =============================================================================

// R04.FR.50 - FreqWatt curve must have yUnit = PctMaxW or PctWAvail
TEST_F(DERControlTest, SetDERControl_FreqWatt_WrongYUnit_Rejected) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-fw-bad-unit";
    req.controlType = DERControlEnum::FreqWatt;
    DERCurve curve;
    curve.priority = 0;
    curve.yUnit = DERUnitEnum::PctMaxVar; // Wrong, should be PctMaxW or PctWAvail
    DERCurvePoints p1;
    p1.x = 59.0f;
    p1.y = 100.0f;
    curve.curveData = {p1};
    req.curve = curve;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// R04.FR.50 - FreqWatt curve with valid yUnit = PctMaxW accepted
TEST_F(DERControlTest, SetDERControl_FreqWatt_ValidYUnit_Accepted) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-fw-good";
    req.controlType = DERControlEnum::FreqWatt;
    DERCurve curve;
    curve.priority = 0;
    curve.yUnit = DERUnitEnum::PctMaxW;
    DERCurvePoints p1;
    p1.x = 59.0f;
    p1.y = 100.0f;
    curve.curveData = {p1};
    req.curve = curve;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(1);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// R04.FR.52 - VoltVar curve must have yUnit = PctMaxVar or PctVarAvail
TEST_F(DERControlTest, SetDERControl_VoltVar_WrongYUnit_Rejected) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-vv-bad-unit";
    req.controlType = DERControlEnum::VoltVar;
    DERCurve curve;
    curve.priority = 0;
    curve.yUnit = DERUnitEnum::PctMaxW; // Wrong, VoltVar needs PctMaxVar or PctVarAvail
    DERCurvePoints p1;
    p1.x = 0.97f;
    p1.y = 100.0f;
    curve.curveData = {p1};
    req.curve = curve;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// R04.FR.53 - VoltWatt curve must have yUnit = PctMaxW or PctWAvail
TEST_F(DERControlTest, SetDERControl_VoltWatt_WrongYUnit_Rejected) {
    DERControl der_control(functional_block_context);

    auto req = make_volt_watt_curve_request("ctrl-vw-bad-unit", true, 0);
    req.curve.value().yUnit = DERUnitEnum::PctMaxVar; // Wrong

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// R04.FR.50 - FreqWatt curve with valid yUnit = PctWAvail accepted (alternate unit)
TEST_F(DERControlTest, SetDERControl_FreqWatt_PctWAvail_Accepted) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-fw-pctwavail";
    req.controlType = DERControlEnum::FreqWatt;
    DERCurve curve;
    curve.priority = 0;
    curve.yUnit = DERUnitEnum::PctWAvail;
    DERCurvePoints p1;
    p1.x = 59.0f;
    p1.y = 100.0f;
    curve.curveData = {p1};
    req.curve = curve;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(1);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// R04.FR.52 - VoltVar curve with valid yUnit = PctVarAvail accepted (alternate unit)
TEST_F(DERControlTest, SetDERControl_VoltVar_PctVarAvail_Accepted) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-vv-pctvaravail";
    req.controlType = DERControlEnum::VoltVar;
    DERCurve curve;
    curve.priority = 0;
    curve.yUnit = DERUnitEnum::PctVarAvail;
    DERCurvePoints p1;
    p1.x = 0.97f;
    p1.y = 100.0f;
    curve.curveData = {p1};
    req.curve = curve;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(1);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// R04.FR.53 - VoltWatt curve with valid yUnit = PctWAvail accepted (alternate unit)
TEST_F(DERControlTest, SetDERControl_VoltWatt_PctWAvail_Accepted) {
    DERControl der_control(functional_block_context);

    auto req = make_volt_watt_curve_request("ctrl-vw-pctwavail", true, 0);
    req.curve.value().yUnit = DERUnitEnum::PctWAvail;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(1);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// R04.FR.56 - LimitMaxDischarge.powerMonitoringMustTrip curve must have yUnit = Not_Applicable
TEST_F(DERControlTest, SetDERControl_LimitMaxDischarge_PowerMonitoringMustTrip_WrongYUnit_Rejected) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-lmd-pmt-bad";
    req.controlType = DERControlEnum::LimitMaxDischarge;
    LimitMaxDischarge lmd;
    lmd.priority = 0;
    lmd.pctMaxDischargePower = 50.0f;
    DERCurve pmt;
    pmt.priority = 0;
    pmt.yUnit = DERUnitEnum::PctMaxW; // Wrong, must be Not_Applicable
    DERCurvePoints p1;
    p1.x = 1.0f;
    p1.y = 0.0f;
    pmt.curveData = {p1};
    lmd.powerMonitoringMustTrip = pmt;
    req.limitMaxDischarge = lmd;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// R04.FR.56 - LimitMaxDischarge.powerMonitoringMustTrip with yUnit = Not_Applicable accepted
TEST_F(DERControlTest, SetDERControl_LimitMaxDischarge_PowerMonitoringMustTrip_NotApplicable_Accepted) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-lmd-pmt-ok";
    req.controlType = DERControlEnum::LimitMaxDischarge;
    LimitMaxDischarge lmd;
    lmd.priority = 0;
    lmd.pctMaxDischargePower = 50.0f;
    DERCurve pmt;
    pmt.priority = 0;
    pmt.yUnit = DERUnitEnum::Not_Applicable;
    DERCurvePoints p1;
    p1.x = 1.0f;
    p1.y = 0.0f;
    pmt.curveData = {p1};
    lmd.powerMonitoringMustTrip = pmt;
    req.limitMaxDischarge = lmd;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(1);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// =============================================================================
// ReportDERControl population tests (R04.FR.31-32)
// =============================================================================

// ReportDERControl populates curve field from a VoltWatt stored control (R04.FR.31)
TEST_F(DERControlTest, GetDERControl_ReportPopulatesCurveField) {
    DERControl der_control(functional_block_context);

    GetDERControlRequest req;
    req.requestId = 42;
    req.controlType = DERControlEnum::VoltWatt;
    auto msg = make_get_der_control_msg(req);

    // Build stored JSON for a VoltWatt curve control
    json stored;
    stored["isSuperseded"] = false;
    stored["priority"] = 0;
    json request;
    request["isDefault"] = true;
    request["controlId"] = "ctrl-vw-1";
    request["controlType"] = "VoltWatt";
    json curve;
    curve["priority"] = 0;
    curve["yUnit"] = "PctMaxW";
    json point;
    point["x"] = 0.97;
    point["y"] = 100.0;
    curve["curveData"] = json::array({point});
    request["curve"] = curve;
    stored["request"] = request;

    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{stored.dump()}));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<GetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool /*t*/) {
        auto action = call[ocpp::CALL_ACTION].get<std::string>();
        EXPECT_EQ(action, "ReportDERControl");
        auto payload = call[ocpp::CALL_PAYLOAD];
        EXPECT_EQ(payload["requestId"], 42);
        ASSERT_TRUE(payload.contains("curve"));
        EXPECT_EQ(payload["curve"].size(), 1);
        EXPECT_EQ(payload["curve"][0]["id"], "ctrl-vw-1");
        EXPECT_EQ(payload["curve"][0]["curveType"], "VoltWatt");
        EXPECT_EQ(payload["curve"][0]["isDefault"], true);
        EXPECT_EQ(payload["curve"][0]["isSuperseded"], false);
        // Single-message report, tbc should be false or absent
        if (payload.contains("tbc")) {
            EXPECT_FALSE(payload["tbc"].get<bool>());
        }
    }));

    der_control.handle_message(msg);
}

// ReportDERControl populates freqDroop field from a FreqDroop stored control
TEST_F(DERControlTest, GetDERControl_ReportPopulatesFreqDroopField) {
    DERControl der_control(functional_block_context);

    GetDERControlRequest req;
    req.requestId = 7;
    req.controlType = DERControlEnum::FreqDroop;
    auto msg = make_get_der_control_msg(req);

    json stored;
    stored["isSuperseded"] = false;
    stored["priority"] = 0;
    json request;
    request["isDefault"] = true;
    request["controlId"] = "ctrl-fd-1";
    request["controlType"] = "FreqDroop";
    json fd;
    fd["priority"] = 0;
    fd["overFreq"] = 61.0;
    fd["underFreq"] = 59.0;
    fd["overDroop"] = 5.0;
    fd["underDroop"] = 5.0;
    fd["responseTime"] = 3.0;
    request["freqDroop"] = fd;
    stored["request"] = request;

    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{stored.dump()}));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& /*c*/) {}));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool /*t*/) {
        auto payload = call[ocpp::CALL_PAYLOAD];
        ASSERT_TRUE(payload.contains("freqDroop"));
        EXPECT_EQ(payload["freqDroop"].size(), 1);
        EXPECT_EQ(payload["freqDroop"][0]["id"], "ctrl-fd-1");
        EXPECT_FALSE(payload.contains("curve"));
    }));

    der_control.handle_message(msg);
}

// =============================================================================
// Scheduled-check transactional behavior
// =============================================================================

namespace {

std::string make_expired_scheduled_control_json(const std::string& control_id) {
    // A fixed past timestamp + 60 s duration is safely in the past for any test run.
    json j;
    j["isSuperseded"] = false;
    j["priority"] = 5;
    j["startTime"] = "2020-01-01T00:00:00Z";
    j["duration"] = 60.0f;
    json request;
    request["controlId"] = control_id;
    request["controlType"] = "FreqDroop";
    request["isDefault"] = false;
    j["request"] = request;
    return j.dump();
}

} // namespace

// Expiry pass wraps scan + delete inside a single DB transaction; the
// notification is dispatched only after commit so a dispatcher throw can't
// roll back a row whose stop has already been observed by CSMS.
TEST_F(DERControlTest, CheckScheduledControls_ScanAndDeleteRunInsideTransaction) {
    DERControl der_control(functional_block_context);

    auto transaction = std::make_unique<TransactionMock>();
    auto* transaction_raw = transaction.get();

    InSequence seq;
    EXPECT_CALL(database_handler_mock, begin_transaction()).WillOnce(Return(ByMove(std::move(transaction))));
    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(std::optional<bool>(false), std::optional<std::string>(std::nullopt),
                                                   std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{make_expired_scheduled_control_json("ctrl-expired")}));
    EXPECT_CALL(database_handler_mock, delete_der_control("ctrl-expired"));
    EXPECT_CALL(*transaction_raw, commit());
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)); // NotifyDERStartStop(started=false), post-commit

    der_control.check_scheduled_controls();
}

// A row with a malformed startTime does not abort the pass, a subsequent
// good row is still processed.
TEST_F(DERControlTest, CheckScheduledControls_SkipsRowWithUnparseableStartTime) {
    DERControl der_control(functional_block_context);

    json bad;
    bad["isSuperseded"] = false;
    bad["priority"] = 5;
    bad["startTime"] = "not-a-real-timestamp";
    bad["duration"] = 60.0f;
    json bad_request;
    bad_request["controlId"] = "ctrl-bad";
    bad_request["controlType"] = "FreqDroop";
    bad_request["isDefault"] = false;
    bad["request"] = bad_request;

    auto good = make_expired_scheduled_control_json("ctrl-good");

    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{bad.dump(), good}));

    // good must still be processed even though bad throws during parse
    EXPECT_CALL(database_handler_mock, delete_der_control("ctrl-good"));

    // Must not throw out of the timer callback path
    EXPECT_NO_THROW(der_control.check_scheduled_controls());
}

// =============================================================================
// Input bounds (defense in depth against CSMS-supplied payloads)
// =============================================================================

// curveData larger than the configured cap is rejected before persistence.
TEST_F(DERControlTest, SetDERControl_RejectsCurveWithExcessivePoints) {
    DERControl der_control(functional_block_context);

    auto req = make_volt_watt_curve_request("ctrl-huge", true, 0);
    req.curve->curveData.clear();
    for (int i = 0; i < 1024; ++i) {
        DERCurvePoints p;
        p.x = static_cast<float>(i);
        p.y = 0.0F;
        req.curve->curveData.push_back(p);
    }
    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// Non-finite duration is rejected before persistence.
TEST_F(DERControlTest, SetDERControl_RejectsNonFiniteDuration) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-inf-dur", false, 0);
    req.freqDroop->duration = std::numeric_limits<float>::infinity();
    req.freqDroop->startTime = ocpp::DateTime();
    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// Negative duration is rejected before persistence.
TEST_F(DERControlTest, SetDERControl_RejectsNegativeDuration) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-neg-dur", false, 0);
    req.freqDroop->duration = -1.0F;
    req.freqDroop->startTime = ocpp::DateTime();
    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// Negative priority is rejected before persistence.
TEST_F(DERControlTest, SetDERControl_RejectsNegativePriority) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-neg-prio", true, -1);
    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// R04.FR.08: A new scheduled control with strictly higher priority value than
// an existing active same-type control is stored isSuperseded=true and the
// response's supersededIds echoes the NEW controlId. The existing control is
// not touched.
TEST_F(DERControlTest, SetDERControl_FR08_NewLowerPriorityScheduledIsSelfSuperseded) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-new-low", /*is_default=*/false, /*priority=*/7);
    req.freqDroop->startTime = ocpp::DateTime();
    req.freqDroop->duration = 3600.0F;
    auto msg = make_set_der_control_msg(req);

    // Existing scheduled active (priority 3 is higher priority than new's 7).
    std::string existing =
        R"({"priority":3,"isSuperseded":false,"request":{"controlId":"ctrl-existing-high","controlType":"FreqDroop","isDefault":false}})";
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{existing}));

    // Existing must NOT be flipped
    EXPECT_CALL(database_handler_mock, update_der_control_superseded(_, _)).Times(0);
    // New row goes in with is_superseded=true
    EXPECT_CALL(database_handler_mock,
                insert_or_update_der_control("ctrl-new-low", false, "FreqDroop", /*is_superseded=*/true, 7, _, _, _));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
        ASSERT_TRUE(response.supersededIds.has_value());
        ASSERT_EQ(response.supersededIds->size(), 1u);
        EXPECT_EQ(response.supersededIds->at(0).get(), "ctrl-new-low");
    }));

    der_control.handle_message(msg);
}

// Supersede requires strictly lower priority value, ties do not supersede.
TEST_F(DERControlTest, SetDERControl_EqualPriorityDoesNotSupersede) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-new-equal", true, 5);
    auto msg = make_set_der_control_msg(req);

    std::string existing =
        R"({"isSuperseded":false,"priority":5,"request":{"controlId":"ctrl-existing-equal","controlType":"FreqDroop","isDefault":true}})";
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{existing}));

    EXPECT_CALL(database_handler_mock, update_der_control_superseded(_, _)).Times(0);
    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
        EXPECT_FALSE(response.supersededIds.has_value());
    }));

    der_control.handle_message(msg);
}

// =============================================================================
// R04.FR.06 / FR.07 — the discriminator between "immediate supersede" and
// "deferred supersede" is whether the EXISTING control is already active, not
// whether the new control's startTime is in the future. FR.06 covers the
// existing-not-yet-active case (immediate flip, even when new.startTime is
// later than the existing one's). FR.07 covers the existing-already-active
// case (defer the flip until new.startTime).
// =============================================================================

namespace {

// Return an RFC 3339 timestamp `seconds` from now.
std::string rfc3339_offset_from_now(int seconds) {
    auto tp = ocpp::DateTime().to_time_point() + std::chrono::seconds(seconds);
    return ocpp::DateTime(tp).to_rfc3339();
}

} // namespace

// R04.FR.07: when the to-be-superseded control is CURRENTLY ACTIVE (past
// startTime, before expiry) AND the new control's startTime is in the future,
// the supersede is deferred until the new control's startTime arrives. The
// response omits supersededIds (TC_R_105 step 10); the existing row stays
// isSuperseded=false; a pending-supersede link is recorded for the timer.
TEST_F(DERControlTest, SetDERControl_FR07_ExistingActiveDefersSupersede) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-new-future", /*is_default=*/false, /*priority=*/1);
    req.freqDroop->startTime = ocpp::DateTime(rfc3339_offset_from_now(3600));
    req.freqDroop->duration = 3600.0F;
    auto msg = make_set_der_control_msg(req);

    // Existing scheduled control truly active: started 60 s ago, runs another
    // 30 minutes. Priority 5 is lower priority (higher numeric value) than new's 1.
    std::string existing =
        R"({"priority":5,"isSuperseded":false,"request":{"controlId":"ctrl-existing-active","controlType":"FreqDroop","isDefault":false},)"
        R"("startTime":")" +
        rfc3339_offset_from_now(-60) + R"(","duration":1800.0})";
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{existing}));

    // Existing must NOT be flipped immediately — deferred until new.startTime.
    EXPECT_CALL(database_handler_mock, update_der_control_superseded(_, _)).Times(0);
    // New row goes in with isSuperseded=false; pending-supersede link recorded.
    EXPECT_CALL(database_handler_mock, insert_or_update_der_control("ctrl-new-future", false, "FreqDroop",
                                                                    /*is_superseded=*/false, 1, _, _, _));
    EXPECT_CALL(database_handler_mock, set_der_control_pending_supersede("ctrl-new-future", "ctrl-existing-active"));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
        // TC_R_105 step 10: when the supersede is deferred, supersededIds is OMITTED
        // from the response; CSMS will be told via NotifyDERStartStop later.
        EXPECT_FALSE(response.supersededIds.has_value());
    }));

    der_control.handle_message(msg);
}

// R04.FR.06: when the to-be-superseded control is NOT YET STARTED (existing
// startTime is still in the future) AND the new control also has a future
// startTime, supersede is IMMEDIATE. This is the TC_R_104 case. The existing's
// isSuperseded flag is flipped right away and the response's supersededIds
// echoes the existing controlId.
TEST_F(DERControlTest, SetDERControl_FR06_ExistingNotYetStartedFlipsImmediatelyEvenWithFutureNew) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-new-later", /*is_default=*/false, /*priority=*/4);
    req.freqDroop->startTime = ocpp::DateTime(rfc3339_offset_from_now(4 * 3600));
    req.freqDroop->duration = 300.0F;
    auto msg = make_set_der_control_msg(req);

    // Existing scheduled control NOT yet started (startTime 2h from now).
    // Priority 5 is lower priority than new's 4 → new wins.
    std::string existing =
        R"({"priority":5,"isSuperseded":false,"request":{"controlId":"ctrl-existing-future","controlType":"FreqDroop","isDefault":false},)"
        R"("startTime":")" +
        rfc3339_offset_from_now(2 * 3600) + R"(","duration":300.0})";
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{existing}));

    // R04.FR.06: existing not yet active → immediate flip.
    EXPECT_CALL(database_handler_mock, update_der_control_superseded("ctrl-existing-future", true));
    // No pending-supersede link is recorded.
    EXPECT_CALL(database_handler_mock, set_der_control_pending_supersede(_, _)).Times(0);
    // New row goes in with isSuperseded=false.
    EXPECT_CALL(database_handler_mock, insert_or_update_der_control("ctrl-new-later", false, "FreqDroop",
                                                                    /*is_superseded=*/false, 4, _, _, _));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
        // TC_R_104 step 16: response.supersededIds[0] = id of immediately-superseded existing.
        ASSERT_TRUE(response.supersededIds.has_value());
        ASSERT_EQ(response.supersededIds->size(), 1u);
        EXPECT_EQ(response.supersededIds->at(0).get(), "ctrl-existing-future");
    }));

    der_control.handle_message(msg);
}

// R04.FR.20/21: The scheduled-check pass emits NotifyDERStartStop(started=true)
// the first time it sees a scheduled control whose startTime has arrived but
// whose STARTED_NOTIFIED flag is still 0. The row is then marked notified so
// subsequent runs don't re-emit.
TEST_F(DERControlTest, CheckScheduledControls_FR20_NotifiesStartWhenStartTimeBecomesCurrent) {
    DERControl der_control(functional_block_context);

    EXPECT_CALL(database_handler_mock, get_der_control_pending_supersede_activations(_))
        .WillOnce(Return(std::vector<DatabaseHandlerInterface::PendingSupersedeActivation>{}));
    EXPECT_CALL(database_handler_mock, get_der_controls_needing_start_notify(_))
        .WillOnce(Return(std::vector<std::string>{"ctrl-just-started"}));
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));

    EXPECT_CALL(database_handler_mock, mark_der_control_started_notified("ctrl-just-started"));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool /*t*/) {
        auto payload = call[ocpp::CALL_PAYLOAD];
        EXPECT_EQ(payload["controlId"], "ctrl-just-started");
        EXPECT_EQ(payload["started"], true);
    }));

    der_control.check_scheduled_controls();
}

// R04.FR.07: When the scheduled check observes a pending-supersede activation
// whose startTime is at or before now, it flips the target control to
// isSuperseded=true, clears the pending pointer, and notifies start/stop.
TEST_F(DERControlTest, CheckScheduledControls_FR07_ActivatesDeferredSupersede) {
    DERControl der_control(functional_block_context);

    DatabaseHandlerInterface::PendingSupersedeActivation activation;
    activation.new_id = "ctrl-new-now";
    activation.existing_id = "ctrl-existing-superseded";
    EXPECT_CALL(database_handler_mock, get_der_control_pending_supersede_activations(_))
        .WillOnce(Return(std::vector<DatabaseHandlerInterface::PendingSupersedeActivation>{activation}));
    // No expired rows.
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));

    EXPECT_CALL(database_handler_mock, update_der_control_superseded("ctrl-existing-superseded", true));
    EXPECT_CALL(database_handler_mock, clear_der_control_pending_supersede("ctrl-new-now"));

    // R04.FR.21 + TC_R_105 step 11: exactly one NotifyDERStartStop is dispatched
    // — started=true on the new control with supersededIds pointing at the
    // existing one. The spec does not mandate a started=false on the existing
    // control here; that's reserved for FR.22 genuine expiry.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool /*t*/) {
        auto action = call[ocpp::CALL_ACTION].get<std::string>();
        EXPECT_EQ(action, "NotifyDERStartStop");
        auto payload = call[ocpp::CALL_PAYLOAD];
        EXPECT_EQ(payload["controlId"], "ctrl-new-now");
        EXPECT_TRUE(payload["started"].get<bool>());
        ASSERT_TRUE(payload.contains("supersededIds"));
        EXPECT_EQ(payload["supersededIds"].size(), 1);
        EXPECT_EQ(payload["supersededIds"][0], "ctrl-existing-superseded");
    }));

    der_control.check_scheduled_controls();
}

// =============================================================================
// H1: check_scheduled_controls must commit DB state BEFORE dispatching any
// NotifyDERStartStop. Otherwise a mid-pass throw (e.g. CiString<36> construction
// on a malformed persisted id, or a dispatcher exception) can roll back DB rows
// for which the CSMS has already observed a start/stop notification.
// =============================================================================

// Expiry pass: the DB row is deleted AND the transaction committed before the
// corresponding NotifyDERStartStop(started=false) is dispatched.
TEST_F(DERControlTest, CheckScheduledControls_CommitsBeforeDispatchingExpiryNotification) {
    DERControl der_control(functional_block_context);

    auto transaction = std::make_unique<TransactionMock>();
    auto* transaction_raw = transaction.get();

    InSequence seq;
    EXPECT_CALL(database_handler_mock, begin_transaction()).WillOnce(Return(ByMove(std::move(transaction))));
    EXPECT_CALL(database_handler_mock, get_der_control_pending_supersede_activations(_))
        .WillOnce(Return(std::vector<DatabaseHandlerInterface::PendingSupersedeActivation>{}));
    EXPECT_CALL(database_handler_mock, get_der_controls_needing_start_notify(_))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(std::optional<bool>(false), std::optional<std::string>(std::nullopt),
                                                   std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{make_expired_scheduled_control_json("ctrl-expired")}));
    EXPECT_CALL(database_handler_mock, delete_der_control("ctrl-expired"));
    EXPECT_CALL(*transaction_raw, commit());
    // Dispatch MUST happen only after the commit.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _));

    der_control.check_scheduled_controls();
}

// Start-notify pass: STARTED_NOTIFIED flag is written AND the transaction
// committed before the NotifyDERStartStop(started=true) is dispatched. If a
// dispatcher throw were to race here, the DB flag is already durable, so the
// next run won't re-emit the same start.
TEST_F(DERControlTest, CheckScheduledControls_CommitsBeforeDispatchingStartNotification) {
    DERControl der_control(functional_block_context);

    auto transaction = std::make_unique<TransactionMock>();
    auto* transaction_raw = transaction.get();

    InSequence seq;
    EXPECT_CALL(database_handler_mock, begin_transaction()).WillOnce(Return(ByMove(std::move(transaction))));
    EXPECT_CALL(database_handler_mock, get_der_control_pending_supersede_activations(_))
        .WillOnce(Return(std::vector<DatabaseHandlerInterface::PendingSupersedeActivation>{}));
    EXPECT_CALL(database_handler_mock, get_der_controls_needing_start_notify(_))
        .WillOnce(Return(std::vector<std::string>{"ctrl-just-started"}));
    EXPECT_CALL(database_handler_mock, mark_der_control_started_notified("ctrl-just-started"));
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(*transaction_raw, commit());
    // Dispatch MUST happen only after commit.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _));

    der_control.check_scheduled_controls();
}

// Pending-supersede activation: DB flip + pending-clear + mark-notified + commit
// all happen before the start-notification on the new control is dispatched.
// Per R04.FR.21 the spec only mandates a started=true on the new control with
// supersededIds; no started=false is sent on the existing control here.
TEST_F(DERControlTest, CheckScheduledControls_CommitsBeforeDispatchingSupersedeActivation) {
    DERControl der_control(functional_block_context);

    auto transaction = std::make_unique<TransactionMock>();
    auto* transaction_raw = transaction.get();

    DatabaseHandlerInterface::PendingSupersedeActivation activation;
    activation.new_id = "ctrl-new-now";
    activation.existing_id = "ctrl-existing-superseded";

    InSequence seq;
    EXPECT_CALL(database_handler_mock, begin_transaction()).WillOnce(Return(ByMove(std::move(transaction))));
    EXPECT_CALL(database_handler_mock, get_der_control_pending_supersede_activations(_))
        .WillOnce(Return(std::vector<DatabaseHandlerInterface::PendingSupersedeActivation>{activation}));
    EXPECT_CALL(database_handler_mock, update_der_control_superseded("ctrl-existing-superseded", true));
    EXPECT_CALL(database_handler_mock, clear_der_control_pending_supersede("ctrl-new-now"));
    EXPECT_CALL(database_handler_mock, mark_der_control_started_notified("ctrl-new-now"));
    EXPECT_CALL(database_handler_mock, get_der_controls_needing_start_notify(_))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(*transaction_raw, commit());
    // Single notification (start-new) fires only after commit.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(1);

    der_control.check_scheduled_controls();
}

// If the dispatcher throws partway through flushing queued notifications, the
// outer catch must not log "DER scheduled-control check aborted" — the DB was
// already committed. The current code (pre-fix) dispatches inside the
// transaction, which rolls the DB back on throw leaving CSMS + DB desynced.
TEST_F(DERControlTest, CheckScheduledControls_DispatcherThrowsAfterCommitDoesNotRollback) {
    DERControl der_control(functional_block_context);

    auto transaction = std::make_unique<TransactionMock>();
    auto* transaction_raw = transaction.get();

    InSequence seq;
    EXPECT_CALL(database_handler_mock, begin_transaction()).WillOnce(Return(ByMove(std::move(transaction))));
    EXPECT_CALL(database_handler_mock, get_der_control_pending_supersede_activations(_))
        .WillOnce(Return(std::vector<DatabaseHandlerInterface::PendingSupersedeActivation>{}));
    EXPECT_CALL(database_handler_mock, get_der_controls_needing_start_notify(_))
        .WillOnce(Return(std::vector<std::string>{"ctrl-a", "ctrl-b"}));
    // Both marks happen before commit, before any dispatch.
    EXPECT_CALL(database_handler_mock, mark_der_control_started_notified("ctrl-a"));
    EXPECT_CALL(database_handler_mock, mark_der_control_started_notified("ctrl-b"));
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));
    // Commit happens before dispatch.
    EXPECT_CALL(*transaction_raw, commit());
    // The second dispatch throws. The first has already been observed by CSMS;
    // the DB is already committed so state stays consistent.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json&, bool) {
        throw std::runtime_error("simulated dispatcher failure");
    }));
    // Rollback MUST NOT be called — the transaction already committed.
    EXPECT_CALL(*transaction_raw, rollback()).Times(0);

    // Exception is caught by the outer try in check_scheduled_controls.
    EXPECT_NO_THROW(der_control.check_scheduled_controls());
}

// =============================================================================
// H2: handle_set_der_control's immediate-start branch must mark the row
// STARTED_NOTIFIED before committing, and only dispatch NotifyDERStartStop
// after commit. A crash between commit and dispatch loses a start (recoverable
// by CSMS retry), which is strictly better than the pre-fix window where a
// crash between dispatch and mark-notified causes a duplicate start on the
// next 30 s periodic check.
// =============================================================================

TEST_F(DERControlTest, SetDERControl_ImmediateStart_MarksNotifiedInsideTransactionBeforeDispatch) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-immediate-tx", /*is_default=*/false, /*priority=*/0);
    // startTime = now, duration > 0 → immediate-start branch fires.
    req.freqDroop.value().startTime = ocpp::DateTime();
    req.freqDroop.value().duration = 3600.0f;
    auto msg = make_set_der_control_msg(req);

    auto transaction = std::make_unique<TransactionMock>();
    auto* transaction_raw = transaction.get();

    InSequence seq;
    EXPECT_CALL(database_handler_mock, begin_transaction()).WillOnce(Return(ByMove(std::move(transaction))));
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(database_handler_mock,
                insert_or_update_der_control("ctrl-immediate-tx", false, "FreqDroop", false, 0, _, _, _));
    // STARTED_NOTIFIED flag must be written inside the same transaction, BEFORE commit.
    EXPECT_CALL(database_handler_mock, mark_der_control_started_notified("ctrl-immediate-tx"));
    EXPECT_CALL(*transaction_raw, commit());
    // Response and NotifyDERStartStop dispatches happen strictly after commit.
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _));

    der_control.handle_message(msg);
}

// If immediate-start does not apply (future startTime), no STARTED_NOTIFIED
// mark-up is written: the periodic check owns that flip.
TEST_F(DERControlTest, SetDERControl_FutureStart_DoesNotMarkNotified) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-future", /*is_default=*/false, /*priority=*/0);
    req.freqDroop.value().startTime = ocpp::DateTime(rfc3339_offset_from_now(3600));
    req.freqDroop.value().duration = 3600.0f;
    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _));
    EXPECT_CALL(database_handler_mock, mark_der_control_started_notified(_)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));
    // No NotifyDERStartStop for a future-start control.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    der_control.handle_message(msg);
}

// If the new scheduled control is self-superseded (R04.FR.08), no immediate
// start-notification is emitted and no STARTED_NOTIFIED mark-up is written.
TEST_F(DERControlTest, SetDERControl_SelfSuperseded_DoesNotMarkNotified) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-self-super", /*is_default=*/false, /*priority=*/7);
    req.freqDroop.value().startTime = ocpp::DateTime();
    req.freqDroop.value().duration = 3600.0f;
    auto msg = make_set_der_control_msg(req);

    // Existing higher-priority active control forces new to be self-superseded.
    std::string existing =
        R"({"priority":3,"isSuperseded":false,"request":{"controlId":"ctrl-existing-hp","controlType":"FreqDroop","isDefault":false}})";
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{existing}));
    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, /*is_superseded=*/true, _, _, _, _));
    EXPECT_CALL(database_handler_mock, mark_der_control_started_notified(_)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    der_control.handle_message(msg);
}

// =============================================================================
// M1: non-finite float rejection on every variant
// =============================================================================

// A NaN on a curve point.x / y must fail validation BEFORE persistence so it
// can never round-trip through CONTROL_JSON into a ReportDERControl.
TEST_F(DERControlTest, SetDERControl_Rejects_NaN_In_CurvePoint) {
    DERControl der_control(functional_block_context);

    auto req = make_volt_watt_curve_request("ctrl-nan-curve", /*is_default=*/true, /*priority=*/0);
    ASSERT_TRUE(req.curve.has_value());
    ASSERT_FALSE(req.curve->curveData.empty());
    req.curve->curveData.front().x = std::numeric_limits<float>::quiet_NaN();
    auto msg = make_set_der_control_msg(req);

    // Must NOT reach persistence.
    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// +Infinity on any FreqDroop float must be rejected.
TEST_F(DERControlTest, SetDERControl_Rejects_Inf_In_FreqDroop) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-inf-fd", /*is_default=*/true, /*priority=*/0);
    req.freqDroop->overDroop = std::numeric_limits<float>::infinity();
    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// NaN on a FixedVar.setpoint must be rejected (FixedVar is a supported mode).
TEST_F(DERControlTest, SetDERControl_Rejects_NaN_In_FixedVar) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-nan-fv";
    req.controlType = DERControlEnum::FixedVar;
    FixedVar fv;
    fv.priority = 0;
    fv.setpoint = std::numeric_limits<float>::quiet_NaN();
    fv.unit = DERUnitEnum::PctMaxVar;
    req.fixedVar = fv;
    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// =============================================================================
// M2: DER_CONTROLS row-count cap
// =============================================================================

// When the table is already at MAX_DER_CONTROLS and the controlId is not an
// update of an existing row, the request must be rejected with a statusInfo
// carrying the limit-exceeded reason. Persistence MUST NOT be attempted.
TEST_F(DERControlTest, SetDERControl_Rejects_When_At_Cap) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-overflow", /*is_default=*/true, /*priority=*/0);
    auto msg = make_set_der_control_msg(req);

    constexpr std::size_t kMaxDerControls = 1000;
    EXPECT_CALL(database_handler_mock, count_der_controls()).WillOnce(Return(kMaxDerControls));
    // Not an update: controlId does not already exist.
    EXPECT_CALL(database_handler_mock, get_der_control("ctrl-overflow")).WillOnce(Return(std::nullopt));

    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
        ASSERT_TRUE(response.statusInfo.has_value());
        EXPECT_EQ(response.statusInfo->reasonCode.get(), "InvalidValue");
        ASSERT_TRUE(response.statusInfo->additionalInfo.has_value());
        EXPECT_EQ(response.statusInfo->additionalInfo->get(), "DERControlLimitExceeded");
    }));

    der_control.handle_message(msg);
}

// Updating an existing row does not grow the table, so the cap must not block
// it even when count == MAX.
TEST_F(DERControlTest, SetDERControl_Update_Allowed_At_Cap) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-update", /*is_default=*/true, /*priority=*/0);
    auto msg = make_set_der_control_msg(req);

    constexpr std::size_t kMaxDerControls = 1000;
    EXPECT_CALL(database_handler_mock, count_der_controls()).WillOnce(Return(kMaxDerControls));
    // Update: controlId already exists.
    EXPECT_CALL(database_handler_mock, get_der_control("ctrl-update"))
        .WillOnce(Return(std::optional<std::string>{"{}"}));
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(1);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// =============================================================================
// M3: startTime beyond schedule horizon
// =============================================================================

// A scheduled control with startTime > now + MAX_SCHEDULE_HORIZON_SECONDS must
// be rejected. Otherwise a CSMS could seed a zombie row that lives until year
// 2999 unless explicit Clear arrives.
TEST_F(DERControlTest, SetDERControl_Rejects_StartTime_Beyond_Horizon) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-far-future", /*is_default=*/false, /*priority=*/0);
    // MAX_SCHEDULE_HORIZON_SECONDS is one year; go 2 years out.
    auto tp = ocpp::DateTime().to_time_point() + std::chrono::hours(24 * 365 * 2);
    req.freqDroop->startTime = ocpp::DateTime(tp);
    req.freqDroop->duration = 60.0F;
    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// =============================================================================
// ReportDERControl multi-row ordering and chunking (R04.FR.31, R04.FR.32)
// =============================================================================

// TC_R_106 step 13: a Report that returns two rows of the same controlType,
// one active (isSuperseded=false) and one self-superseded by R04.FR.08
// (isSuperseded=true). Both rows must appear in the same message with the
// expected isSuperseded bit pattern in row order.
TEST_F(DERControlTest, GetDERControl_ReportOrdersMultiRowByIsSuperseded) {
    DERControl der_control(functional_block_context);

    GetDERControlRequest req;
    req.requestId = 99;
    req.controlType = DERControlEnum::FreqDroop;
    auto msg = make_get_der_control_msg(req);

    auto make_stored = [](const std::string& id, int32_t priority, bool is_superseded) {
        json stored;
        stored["priority"] = priority;
        stored["isSuperseded"] = is_superseded;
        json request;
        request["isDefault"] = false;
        request["controlId"] = id;
        request["controlType"] = "FreqDroop";
        json fd;
        fd["priority"] = priority;
        fd["overFreq"] = 61.0;
        fd["underFreq"] = 59.0;
        fd["overDroop"] = 5.0;
        fd["underDroop"] = 5.0;
        fd["responseTime"] = 3.0;
        request["freqDroop"] = fd;
        stored["request"] = request;
        return stored.dump();
    };

    // Active row first, self-superseded row second — the order der_control
    // would receive them after the FR.08 path persists both.
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{
            make_stored("ctrl-active", /*priority=*/2, /*is_superseded=*/false),
            make_stored("ctrl-self-superseded", /*priority=*/5, /*is_superseded=*/true),
        }));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<GetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool /*t*/) {
        auto action = call[ocpp::CALL_ACTION].get<std::string>();
        EXPECT_EQ(action, "ReportDERControl");
        auto payload = call[ocpp::CALL_PAYLOAD];
        ASSERT_TRUE(payload.contains("freqDroop"));
        ASSERT_EQ(payload["freqDroop"].size(), 2);
        EXPECT_EQ(payload["freqDroop"][0]["id"], "ctrl-active");
        EXPECT_EQ(payload["freqDroop"][0]["isSuperseded"], false);
        EXPECT_EQ(payload["freqDroop"][1]["id"], "ctrl-self-superseded");
        EXPECT_EQ(payload["freqDroop"][1]["isSuperseded"], true);
        if (payload.contains("tbc")) {
            EXPECT_FALSE(payload["tbc"].get<bool>());
        }
    }));

    der_control.handle_message(msg);
}

// R04.FR.32: when more than MAX_CONTROLS_PER_REPORT_MESSAGE controls match,
// the report is split. tbc must be true on every message except the last.
// The chunking constant is internal (10), so we feed 11 rows: expect two
// dispatches with tbc=true then tbc=false.
TEST_F(DERControlTest, GetDERControl_ReportChunksAndSetsTbc) {
    DERControl der_control(functional_block_context);

    GetDERControlRequest req;
    req.requestId = 11;
    req.controlType = DERControlEnum::FreqDroop;
    auto msg = make_get_der_control_msg(req);

    auto make_stored = [](const std::string& id) {
        json stored;
        stored["priority"] = 0;
        stored["isSuperseded"] = false;
        json request;
        request["isDefault"] = false;
        request["controlId"] = id;
        request["controlType"] = "FreqDroop";
        json fd;
        fd["priority"] = 0;
        fd["overFreq"] = 61.0;
        fd["underFreq"] = 59.0;
        fd["overDroop"] = 5.0;
        fd["underDroop"] = 5.0;
        fd["responseTime"] = 3.0;
        request["freqDroop"] = fd;
        stored["request"] = request;
        return stored.dump();
    };

    std::vector<std::string> stored;
    stored.reserve(11);
    for (int i = 0; i < 11; ++i) {
        stored.emplace_back(make_stored("ctrl-" + std::to_string(i)));
    }
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _)).WillOnce(Return(std::move(stored)));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& /*c*/) {}));

    InSequence sequence;
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool /*t*/) {
        auto payload = call[ocpp::CALL_PAYLOAD];
        EXPECT_EQ(payload["requestId"], 11);
        ASSERT_TRUE(payload.contains("freqDroop"));
        EXPECT_EQ(payload["freqDroop"].size(), 10);
        ASSERT_TRUE(payload.contains("tbc"));
        EXPECT_TRUE(payload["tbc"].get<bool>());
    }));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool /*t*/) {
        auto payload = call[ocpp::CALL_PAYLOAD];
        EXPECT_EQ(payload["requestId"], 11);
        ASSERT_TRUE(payload.contains("freqDroop"));
        EXPECT_EQ(payload["freqDroop"].size(), 1);
        // tbc on the final message must be false (or absent — payload-equivalent).
        if (payload.contains("tbc")) {
            EXPECT_FALSE(payload["tbc"].get<bool>());
        }
    }));

    der_control.handle_message(msg);
}

// =============================================================================
// RFC3339 timezone offset handling
// =============================================================================

// All time comparisons in der_control.cpp run on UTC time_points obtained from
// ocpp::DateTime. A non-Z RFC3339 input must produce the same instant as the
// equivalent Z-suffixed string; otherwise the supersede analysis (and the
// schedule-horizon / immediate-start branches) would silently misclassify
// startTimes whose CSMS sent them in a local offset.
TEST_F(DERControlTest, DateTime_NonUtcOffset_ParsesToSameInstantAsUtc) {
    const ocpp::DateTime utc_z("2026-06-01T10:00:00Z");
    const ocpp::DateTime offset_plus_2("2026-06-01T12:00:00+02:00");
    const ocpp::DateTime offset_minus_5("2026-06-01T05:00:00-05:00");

    EXPECT_EQ(utc_z.to_time_point(), offset_plus_2.to_time_point());
    EXPECT_EQ(utc_z.to_time_point(), offset_minus_5.to_time_point());
}

// =============================================================================
// TC_R_104 — outbound ReportDERControl message registration
// =============================================================================

// Bug regression: MessageType::ReportDERControl[Response] were missing from the
// enum and from both string conversions, which caused the outbound
// Call<ReportDERControlRequest> following a successful GetDERControl to throw
// inside the dispatcher and emit a FormationViolation CallError on the wire.
TEST(DERControlMessageType, ReportDERControl_RoundTripsThroughStringConversions) {
    EXPECT_EQ(conversions::messagetype_to_string(MessageType::ReportDERControl), "ReportDERControl");
    EXPECT_EQ(conversions::messagetype_to_string(MessageType::ReportDERControlResponse), "ReportDERControlResponse");
    EXPECT_EQ(conversions::string_to_messagetype("ReportDERControl"), MessageType::ReportDERControl);
    EXPECT_EQ(conversions::string_to_messagetype("ReportDERControlResponse"), MessageType::ReportDERControlResponse);
}

// =============================================================================
// TC_R_105 — FR.22 expiry-notify must echo displacedIds (the supersededIds
// originally reported on FR.20 start). Persisted in CONTROL_JSON.displacedIds.
// =============================================================================

// Expiry-notify reads CONTROL_JSON.displacedIds and copies the ids into
// NotifyDERStartStop(started=false).supersededIds, so the CSMS sees the same
// supersede relationship on stop that it observed on start.
TEST_F(DERControlTest, CheckScheduledControls_FR22_ExpiryNotify_EchoesDisplacedIds) {
    DERControl der_control(functional_block_context);

    json expired;
    expired["isSuperseded"] = false;
    expired["priority"] = 5;
    expired["startTime"] = "2020-01-01T00:00:00Z";
    expired["duration"] = 60.0F;
    expired["displacedIds"] = json::array({"ctrl-A", "ctrl-B", "ctrl-C"});
    json req;
    req["controlId"] = "ctrl-expired";
    req["controlType"] = "FreqDroop";
    req["isDefault"] = false;
    expired["request"] = req;

    EXPECT_CALL(database_handler_mock, get_der_control_pending_supersede_activations(_))
        .WillOnce(Return(std::vector<DatabaseHandlerInterface::PendingSupersedeActivation>{}));
    EXPECT_CALL(database_handler_mock, get_der_controls_needing_start_notify(_))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{expired.dump()}));
    EXPECT_CALL(database_handler_mock, delete_der_control("ctrl-expired"));

    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool /*t*/) {
        auto action = call[ocpp::CALL_ACTION].get<std::string>();
        EXPECT_EQ(action, "NotifyDERStartStop");
        auto payload = call[ocpp::CALL_PAYLOAD];
        EXPECT_EQ(payload["controlId"], "ctrl-expired");
        EXPECT_FALSE(payload["started"].get<bool>());
        ASSERT_TRUE(payload.contains("supersededIds"));
        ASSERT_EQ(payload["supersededIds"].size(), 3);
        EXPECT_EQ(payload["supersededIds"][0], "ctrl-A");
        EXPECT_EQ(payload["supersededIds"][1], "ctrl-B");
        EXPECT_EQ(payload["supersededIds"][2], "ctrl-C");
    }));

    der_control.check_scheduled_controls();
}

// An expired row with no displacedIds field (e.g. a control nobody else
// displaced) emits stop without supersededIds.
TEST_F(DERControlTest, CheckScheduledControls_FR22_ExpiryNotify_OmitsSupersededIds_WhenAbsent) {
    DERControl der_control(functional_block_context);

    json expired;
    expired["isSuperseded"] = false;
    expired["priority"] = 5;
    expired["startTime"] = "2020-01-01T00:00:00Z";
    expired["duration"] = 60.0F;
    json req;
    req["controlId"] = "ctrl-solo";
    req["controlType"] = "FreqDroop";
    req["isDefault"] = false;
    expired["request"] = req;

    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{expired.dump()}));
    EXPECT_CALL(database_handler_mock, delete_der_control("ctrl-solo"));

    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool /*t*/) {
        auto payload = call[ocpp::CALL_PAYLOAD];
        EXPECT_EQ(payload["controlId"], "ctrl-solo");
        EXPECT_FALSE(payload["started"].get<bool>());
        EXPECT_FALSE(payload.contains("supersededIds"));
    }));

    der_control.check_scheduled_controls();
}

// =============================================================================
// Recent fix: SetDERControl with immediate-supersede must persist displacedIds
// to CONTROL_JSON, AND cap both response.supersededIds and persisted
// displacedIds at 24 (OCPP 2.1 schema cardinality).
// =============================================================================

// Single immediate-supersede: persisted CONTROL_JSON.displacedIds contains the
// existing controlId and the response echoes it in supersededIds.
TEST_F(DERControlTest, SetDERControl_PersistsImmediateSupersedeInDisplacedIds) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-winner", /*is_default=*/true, /*priority=*/0);
    auto msg = make_set_der_control_msg(req);

    // One existing default of strictly higher priority value → new wins, immediate flip.
    std::string existing =
        R"({"priority":5,"isSuperseded":false,"request":{"controlId":"ctrl-loser","controlType":"FreqDroop","isDefault":true}})";
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{existing}));
    EXPECT_CALL(database_handler_mock, update_der_control_superseded("ctrl-loser", true));

    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _))
        .WillOnce(Invoke([](const std::string& /*id*/, bool /*isd*/, const std::string& /*ct*/, bool /*sup*/,
                            int32_t /*p*/, const std::optional<std::string>& /*st*/, const std::optional<float>& /*d*/,
                            const std::string& control_json) {
            auto j = json::parse(control_json);
            ASSERT_TRUE(j.contains("displacedIds")) << control_json;
            ASSERT_TRUE(j["displacedIds"].is_array());
            ASSERT_EQ(j["displacedIds"].size(), 1);
            EXPECT_EQ(j["displacedIds"][0], "ctrl-loser");
        }));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
        ASSERT_TRUE(response.supersededIds.has_value());
        ASSERT_EQ(response.supersededIds->size(), 1u);
        EXPECT_EQ(response.supersededIds->at(0).get(), "ctrl-loser");
    }));

    der_control.handle_message(msg);
}

// 30 existing defaults all lose to a new default of priority 0. Both the
// persisted CONTROL_JSON.displacedIds and the wire response.supersededIds
// must be capped at 24 so neither violates the schema 0..24 cardinality.
TEST_F(DERControlTest, SetDERControl_CapsSupersededIdsAndDisplacedIdsAt24) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-sweep-winner", /*is_default=*/true, /*priority=*/0);
    auto msg = make_set_der_control_msg(req);

    std::vector<std::string> existing_rows;
    existing_rows.reserve(30);
    for (int i = 0; i < 30; ++i) {
        json e;
        e["priority"] = 5;
        e["isSuperseded"] = false;
        json r;
        r["controlId"] = "ctrl-loser-" + std::to_string(i);
        r["controlType"] = "FreqDroop";
        r["isDefault"] = true;
        e["request"] = r;
        existing_rows.emplace_back(e.dump());
    }
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::move(existing_rows)));

    // All 30 existing rows are flipped in the DB (cap is a wire-level concern).
    EXPECT_CALL(database_handler_mock, update_der_control_superseded(_, true)).Times(30);

    EXPECT_CALL(database_handler_mock, insert_or_update_der_control(_, _, _, _, _, _, _, _))
        .WillOnce(Invoke([](const std::string& /*id*/, bool /*isd*/, const std::string& /*ct*/, bool /*sup*/,
                            int32_t /*p*/, const std::optional<std::string>& /*st*/, const std::optional<float>& /*d*/,
                            const std::string& control_json) {
            auto j = json::parse(control_json);
            ASSERT_TRUE(j.contains("displacedIds"));
            EXPECT_EQ(j["displacedIds"].size(), 24u)
                << "displacedIds must be capped at 24 to match OCPP 2.1 §1.48.1 cardinality";
        }));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
        ASSERT_TRUE(response.supersededIds.has_value());
        EXPECT_EQ(response.supersededIds->size(), 24u) << "response.supersededIds must be capped at 24 to match schema";
    }));

    der_control.handle_message(msg);
}

// FR.07 deferred-supersede activation calls append_der_control_displaced_id so
// that the later FR.22 expiry-notify on the new control echoes the displaced
// existing controlId. Locks in the cross-handler wiring between
// check_scheduled_controls and the CONTROL_JSON.displacedIds reader.
TEST_F(DERControlTest, CheckScheduledControls_FR07_AppendsDisplacedIdOnActivation) {
    DERControl der_control(functional_block_context);

    DatabaseHandlerInterface::PendingSupersedeActivation activation;
    activation.new_id = "ctrl-new-active";
    activation.existing_id = "ctrl-displaced";

    EXPECT_CALL(database_handler_mock, get_der_control_pending_supersede_activations(_))
        .WillOnce(Return(std::vector<DatabaseHandlerInterface::PendingSupersedeActivation>{activation}));
    EXPECT_CALL(database_handler_mock, get_der_controls_needing_start_notify(_))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));

    EXPECT_CALL(database_handler_mock, update_der_control_superseded("ctrl-displaced", true));
    EXPECT_CALL(database_handler_mock, clear_der_control_pending_supersede("ctrl-new-active"));
    EXPECT_CALL(database_handler_mock, append_der_control_displaced_id("ctrl-new-active", "ctrl-displaced")).Times(1);
    EXPECT_CALL(database_handler_mock, mark_der_control_started_notified("ctrl-new-active"));

    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _));

    der_control.check_scheduled_controls();
}

// =============================================================================
// DER active-directives callback + notify_der_alarm hooks (provider notifications)
// =============================================================================

// Building the block emits the current active set once. With an empty DER_CONTROLS table the initial
// emit carries an empty set, so the provider learns the standing state immediately at startup.
TEST_F(DERControlTest, BuildingBlock_EmptyTable_EmitsEmptyActiveSetOnce) {
    int emit_count = 0;
    std::vector<SetDERControlRequest> captured;

    // The construction-time emit recomputes from the whole table (no filters) and finds it empty.
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(std::optional<bool>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{}));

    DERControl der_control(functional_block_context, [&](const std::vector<SetDERControlRequest>& active) {
        emit_count++;
        captured = active;
    });

    EXPECT_EQ(emit_count, 1);
    EXPECT_TRUE(captured.empty());
}

// Building the block over a DER_CONTROLS table that already holds a standing (persisted-across-boot)
// default control emits that control in the initial active set.
TEST_F(DERControlTest, BuildingBlock_StandingControl_EmitsItInInitialActiveSet) {
    std::vector<SetDERControlRequest> captured;

    std::string standing_row =
        R"({"isSuperseded":false,"priority":0,"request":{"controlId":"ctrl-standing","controlType":"FreqDroop","isDefault":true}})";
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(std::optional<bool>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{standing_row}));

    DERControl der_control(functional_block_context,
                           [&captured](const std::vector<SetDERControlRequest>& active) { captured = active; });

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0].controlId.get(), "ctrl-standing");
    EXPECT_EQ(captured[0].controlType, DERControlEnum::FreqDroop);
}

// republish_active_directives re-runs the existing emit path on demand: it recomputes the current
// active set from the table and invokes the callback again, without any transition having occurred. The
// provider uses this to learn the standing set when a newly-enabled EVSE joins an already-built block.
TEST_F(DERControlTest, RepublishActiveDirectives_ReEmitsCurrentActiveSet) {
    int emit_count = 0;
    std::vector<SetDERControlRequest> captured;

    // Construction-time emit over an empty table.
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(std::optional<bool>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{}));

    DERControl der_control(functional_block_context, [&](const std::vector<SetDERControlRequest>& active) {
        emit_count++;
        captured = active;
    });
    ASSERT_EQ(emit_count, 1);

    // On-demand re-emit: the table now holds a standing default; the callback fires again carrying it.
    std::string standing_row =
        R"({"isSuperseded":false,"priority":0,"request":{"controlId":"ctrl-standing","controlType":"FreqDroop","isDefault":true}})";
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(std::optional<bool>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{standing_row}));

    der_control.republish_active_directives();

    EXPECT_EQ(emit_count, 2);
    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0].controlId.get(), "ctrl-standing");
}

// Default Set, then a higher-priority same-type Set, then a Clear: the active-directives callback fires
// each time carrying the effective set — first the default, then only the winner, then empty.
TEST_F(DERControlTest, ActiveDirectivesEmittedOnTransitions) {
    std::vector<SetDERControlRequest> captured;
    DERControl der_control(functional_block_context,
                           [&captured](const std::vector<SetDERControlRequest>& active) { captured = active; });

    // CONTROL_JSON for a default FreqDroop control as it would be persisted.
    auto default_row = [](const std::string& id, int32_t priority) {
        json stored;
        stored["isSuperseded"] = false;
        stored["priority"] = priority;
        json request;
        request["isDefault"] = true;
        request["controlId"] = id;
        request["controlType"] = "FreqDroop";
        json fd;
        fd["priority"] = priority;
        fd["overFreq"] = 61.0;
        fd["underFreq"] = 59.0;
        fd["overDroop"] = 5.0;
        fd["underDroop"] = 5.0;
        fd["responseTime"] = 3.0;
        request["freqDroop"] = fd;
        stored["request"] = request;
        return stored.dump();
    };

    // --- Transition 1: accept a new default FreqDroop control (priority 5). ---
    {
        auto req = make_freq_droop_request("ctrl-default", true, 5);
        auto msg = make_set_der_control_msg(req);

        // Supersede analysis (same type / isDefault) — no existing controls.
        EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(std::optional<bool>(true),
                                                                              std::optional<std::string>("FreqDroop"),
                                                                              std::optional<std::string>(std::nullopt)))
            .WillOnce(Return(std::vector<std::string>{}));
        EXPECT_CALL(database_handler_mock,
                    insert_or_update_der_control("ctrl-default", true, "FreqDroop", false, 5, _, _, _));
        // emit_active_directives queries the whole table (no filters) post-commit.
        EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(std::optional<bool>(std::nullopt),
                                                                              std::optional<std::string>(std::nullopt),
                                                                              std::optional<std::string>(std::nullopt)))
            .WillOnce(Return(std::vector<std::string>{default_row("ctrl-default", 5)}));
        EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));

        der_control.handle_message(msg);

        ASSERT_EQ(captured.size(), 1u);
        EXPECT_EQ(captured[0].controlId.get(), "ctrl-default");
        EXPECT_EQ(captured[0].controlType, DERControlEnum::FreqDroop);

        ::testing::Mock::VerifyAndClearExpectations(&database_handler_mock);
        ::testing::Mock::VerifyAndClearExpectations(&mock_dispatcher);
    }

    // --- Transition 2: a higher-priority (lower value) default supersedes it. ---
    {
        auto req = make_freq_droop_request("ctrl-winner", true, 0);
        auto msg = make_set_der_control_msg(req);

        // Supersede analysis returns the existing lower-priority default.
        EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(std::optional<bool>(true),
                                                                              std::optional<std::string>("FreqDroop"),
                                                                              std::optional<std::string>(std::nullopt)))
            .WillOnce(Return(std::vector<std::string>{default_row("ctrl-default", 5)}));
        EXPECT_CALL(database_handler_mock, update_der_control_superseded("ctrl-default", true));
        EXPECT_CALL(database_handler_mock,
                    insert_or_update_der_control("ctrl-winner", true, "FreqDroop", false, 0, _, _, _));
        // Post-commit the table holds the winner active and the old one superseded;
        // emit must surface only the winner.
        std::string superseded_old =
            R"({"isSuperseded":true,"priority":5,"request":{"controlId":"ctrl-default","controlType":"FreqDroop","isDefault":true}})";
        EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(std::optional<bool>(std::nullopt),
                                                                              std::optional<std::string>(std::nullopt),
                                                                              std::optional<std::string>(std::nullopt)))
            .WillOnce(Return(std::vector<std::string>{default_row("ctrl-winner", 0), superseded_old}));
        EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));

        der_control.handle_message(msg);

        ASSERT_EQ(captured.size(), 1u);
        EXPECT_EQ(captured[0].controlId.get(), "ctrl-winner");

        ::testing::Mock::VerifyAndClearExpectations(&database_handler_mock);
        ::testing::Mock::VerifyAndClearExpectations(&mock_dispatcher);
    }

    // --- Transition 3: clear the winner; active set becomes empty. ---
    {
        ClearDERControlRequest req;
        req.isDefault = true;
        req.controlId = "ctrl-winner";
        auto msg = make_clear_der_control_msg(req);

        EXPECT_CALL(database_handler_mock, delete_der_control_by_id_and_default("ctrl-winner", true))
            .WillOnce(Return(true));
        // Post-clear the table is empty.
        EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(std::optional<bool>(std::nullopt),
                                                                              std::optional<std::string>(std::nullopt),
                                                                              std::optional<std::string>(std::nullopt)))
            .WillOnce(Return(std::vector<std::string>{}));
        EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));

        der_control.handle_message(msg);

        EXPECT_TRUE(captured.empty());
    }
}

// Timer path: when check_scheduled_controls deletes an expired control, the post-commit emit recomputes
// from the now-empty table and the captured set is empty.
TEST_F(DERControlTest, ActiveDirectives_ScheduledExpiry_EmitsEmptyActiveSet) {
    bool emitted = false;
    std::vector<SetDERControlRequest> captured;
    DERControl der_control(functional_block_context, [&](const std::vector<SetDERControlRequest>& active) {
        emitted = true;
        captured = active;
    });

    EXPECT_CALL(database_handler_mock, get_der_control_pending_supersede_activations(_))
        .WillOnce(Return(std::vector<DatabaseHandlerInterface::PendingSupersedeActivation>{}));
    EXPECT_CALL(database_handler_mock, get_der_controls_needing_start_notify(_))
        .WillOnce(Return(std::vector<std::string>{}));
    // Scheduled scan returns one expired control; the post-emit recompute (no
    // filters) sees the now-empty table.
    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(std::optional<bool>(false), std::optional<std::string>(std::nullopt),
                                                   std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{make_expired_scheduled_control_json("ctrl-expired")}));
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(std::optional<bool>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(database_handler_mock, delete_der_control("ctrl-expired"));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)); // NotifyDERStartStop(started=false)

    der_control.check_scheduled_controls();

    EXPECT_TRUE(emitted);
    EXPECT_TRUE(captured.empty());
}

// Timer path: when a pending supersede's startTime arrives, check_scheduled_controls flips the displaced
// control and the post-commit emit surfaces only the winner.
TEST_F(DERControlTest, ActiveDirectives_DeferredSupersedeActivation_EmitsWinner) {
    std::vector<SetDERControlRequest> captured;
    DERControl der_control(functional_block_context,
                           [&captured](const std::vector<SetDERControlRequest>& active) { captured = active; });

    DatabaseHandlerInterface::PendingSupersedeActivation activation;
    activation.new_id = "ctrl-winner";
    activation.existing_id = "ctrl-displaced";
    EXPECT_CALL(database_handler_mock, get_der_control_pending_supersede_activations(_))
        .WillOnce(Return(std::vector<DatabaseHandlerInterface::PendingSupersedeActivation>{activation}));
    EXPECT_CALL(database_handler_mock, get_der_controls_needing_start_notify(_))
        .WillOnce(Return(std::vector<std::string>{}));

    // CONTROL_JSON for the just-activated winner: a default FreqDroop so it is
    // unconditionally effective in compute_active_directives.
    std::string winner_row =
        R"({"isSuperseded":false,"priority":0,"request":{"controlId":"ctrl-winner","controlType":"FreqDroop","isDefault":true}})";
    std::string displaced_row =
        R"({"isSuperseded":true,"priority":5,"request":{"controlId":"ctrl-displaced","controlType":"FreqDroop","isDefault":true}})";

    // Scheduled scan (isDefault=false) finds nothing to expire; the post-emit
    // recompute (no filters) sees the winner active and the displaced one
    // superseded.
    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(std::optional<bool>(false), std::optional<std::string>(std::nullopt),
                                                   std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{}));
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(std::optional<bool>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{winner_row, displaced_row}));

    EXPECT_CALL(database_handler_mock, update_der_control_superseded("ctrl-displaced", true));
    EXPECT_CALL(database_handler_mock, clear_der_control_pending_supersede("ctrl-winner"));
    EXPECT_CALL(database_handler_mock, append_der_control_displaced_id("ctrl-winner", "ctrl-displaced"));
    EXPECT_CALL(database_handler_mock, mark_der_control_started_notified("ctrl-winner"));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)); // NotifyDERStartStop(started=true)

    der_control.check_scheduled_controls();

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0].controlId.get(), "ctrl-winner");
}

// compute_active_directives predicate (exercised via a synchronous Clear's post-commit emit): of four
// rows — future-start, expired, superseded, current default — only the current default stays effective.
TEST_F(DERControlTest, ComputeActiveDirectives_FiltersByEffectiveness) {
    std::vector<SetDERControlRequest> captured;
    DERControl der_control(functional_block_context,
                           [&captured](const std::vector<SetDERControlRequest>& active) { captured = active; });

    // Future-start: startTime far ahead → not yet effective.
    json future;
    future["isSuperseded"] = false;
    future["priority"] = 1;
    future["startTime"] = "2999-01-01T00:00:00Z";
    future["duration"] = 3600.0F;
    future["request"] = {{"controlId", "ctrl-future"}, {"controlType", "FreqDroop"}, {"isDefault", false}};

    // Expired: startTime well in the past + short duration → already elapsed.
    json expired;
    expired["isSuperseded"] = false;
    expired["priority"] = 2;
    expired["startTime"] = "2020-01-01T00:00:00Z";
    expired["duration"] = 60.0F;
    expired["request"] = {{"controlId", "ctrl-expired"}, {"controlType", "FreqDroop"}, {"isDefault", false}};

    // Superseded: flagged out of play.
    json superseded;
    superseded["isSuperseded"] = true;
    superseded["priority"] = 3;
    superseded["request"] = {{"controlId", "ctrl-superseded"}, {"controlType", "FreqDroop"}, {"isDefault", true}};

    // Current default: no startTime → unconditionally effective.
    json current;
    current["isSuperseded"] = false;
    current["priority"] = 0;
    current["request"] = {{"controlId", "ctrl-current"}, {"controlType", "FreqDroop"}, {"isDefault", true}};

    ClearDERControlRequest req;
    req.isDefault = false;
    req.controlId = "ctrl-expired";
    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, delete_der_control_by_id_and_default("ctrl-expired", false))
        .WillOnce(Return(true));
    // Post-commit recompute observes all four rows.
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(std::optional<bool>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{future.dump(), expired.dump(), superseded.dump(), current.dump()}));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));

    der_control.handle_message(msg);

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0].controlId.get(), "ctrl-current");
}

// A DB read failure on the emit path is a database fault, not a callback fault: it must neither invoke
// the provider callback nor propagate out of the timer-callback path (else io_context.run() → terminate).
TEST_F(DERControlTest, ActiveDirectives_DbReadThrowsOnEmit_DoesNotInvokeCallback) {
    bool callback_invoked = false;
    DERControl der_control(functional_block_context,
                           [&callback_invoked](const std::vector<SetDERControlRequest>&) { callback_invoked = true; });

    // Disregard the construction-time initial emit; this test is about the timer-path emit.
    callback_invoked = false;

    EXPECT_CALL(database_handler_mock, get_der_control_pending_supersede_activations(_))
        .WillOnce(Return(std::vector<DatabaseHandlerInterface::PendingSupersedeActivation>{}));
    EXPECT_CALL(database_handler_mock, get_der_controls_needing_start_notify(_))
        .WillOnce(Return(std::vector<std::string>{}));
    // Scheduled scan finds one expired control → a transition occurs → emit runs.
    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(std::optional<bool>(false), std::optional<std::string>(std::nullopt),
                                                   std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{make_expired_scheduled_control_json("ctrl-expired")}));
    // The post-commit recompute read throws a DB fault.
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(std::optional<bool>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt),
                                                                          std::optional<std::string>(std::nullopt)))
        .WillOnce(::testing::Throw(everest::db::QueryExecutionException("simulated DB read failure")));
    EXPECT_CALL(database_handler_mock, delete_der_control("ctrl-expired"));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)); // NotifyDERStartStop(started=false)

    EXPECT_NO_THROW(der_control.check_scheduled_controls());

    EXPECT_FALSE(callback_invoked) << "DB read failure must not be surfaced to the provider callback";
}

// notify_der_alarm dispatches a Call<NotifyDERAlarmRequest> via the message dispatcher.
TEST_F(DERControlTest, NotifyDerAlarm_DispatchesNotifyDERAlarmRequest) {
    DERControl der_control(functional_block_context);

    NotifyDERAlarmRequest req;
    req.controlType = DERControlEnum::FreqDroop;
    req.timestamp = ocpp::DateTime("2020-01-01T00:00:00Z");
    req.gridEventFault = GridEventFaultEnum::OverFrequency;

    EXPECT_CALL(mock_dispatcher, dispatch_call(_, false)).WillOnce(Invoke([](const json& call, bool /*t*/) {
        auto action = call[ocpp::CALL_ACTION].get<std::string>();
        EXPECT_EQ(action, "NotifyDERAlarm");
        auto payload = call[ocpp::CALL_PAYLOAD];
        EXPECT_EQ(payload["controlType"], "FreqDroop");
        EXPECT_EQ(payload["gridEventFault"], "OverFrequency");
    }));

    der_control.notify_der_alarm(req);
}

// =============================================================================
// Provider-callback fault isolation: a throwing provider callback on the timer
// thread must not escape to io_context.run() → std::terminate().
// =============================================================================

// A throwing active-directives callback must not propagate out of check_scheduled_controls.
TEST_F(DERControlTest, ActiveDirectives_ThrowingOnTimerPath_DoesNotTerminate) {
    DERControl der_control(functional_block_context, [](const std::vector<SetDERControlRequest>& /*active*/) {
        throw std::runtime_error("provider blew up");
    });

    EXPECT_CALL(database_handler_mock, get_der_control_pending_supersede_activations(_))
        .WillOnce(Return(std::vector<DatabaseHandlerInterface::PendingSupersedeActivation>{}));
    EXPECT_CALL(database_handler_mock, get_der_controls_needing_start_notify(_))
        .WillOnce(Return(std::vector<std::string>{}));
    // One expired control drives a transition, so emit_active_directives runs.
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillRepeatedly(Return(std::vector<std::string>{make_expired_scheduled_control_json("ctrl-expired")}));
    EXPECT_CALL(database_handler_mock, delete_der_control("ctrl-expired"));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)); // NotifyDERStartStop(started=false)

    EXPECT_NO_THROW(der_control.check_scheduled_controls());
}

// No callback registered: ClearDERControl by id does zero extra reads (no get_der_control, no recompute).
TEST_F(DERControlTest, NoCallback_ClearById_DoesNotReadForReconstruction) {
    DERControl der_control(functional_block_context); // no callback

    ClearDERControlRequest req;
    req.isDefault = false;
    req.controlId = "ctrl-x";
    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, get_der_control(_)).Times(0);
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _)).Times(0);
    EXPECT_CALL(database_handler_mock, delete_der_control_by_id_and_default("ctrl-x", false)).WillOnce(Return(true));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));

    der_control.handle_message(msg);
}

// No callback registered: ClearDERControl by criteria does not recompute the active set.
TEST_F(DERControlTest, NoCallback_ClearByCriteria_DoesNotPreReadForReconstruction) {
    DERControl der_control(functional_block_context); // no callback

    ClearDERControlRequest req;
    req.isDefault = false;
    req.controlType = DERControlEnum::FreqDroop;
    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _)).Times(0);
    EXPECT_CALL(database_handler_mock,
                delete_der_controls_matching_criteria(false, std::optional<std::string>("FreqDroop")))
        .WillOnce(Return(1));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));

    der_control.handle_message(msg);
}
