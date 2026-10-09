// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Pins how the OCPP 1.6 chargepoint hands raised and cleared errors to libocpp. What is converted is
// pinned by the freeze tests; this covers where it goes.

#include "v16_error_test_helpers.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

using namespace ocpp_multi;
using namespace v16_error_test;
using testing::_;
using testing::Eq;
using testing::MockFunction;

GenericChargePointInterface::EventInfo make_event(const Everest::error::Error& error, std::int32_t evse_id,
                                                  bool cleared) {
    GenericChargePointInterface::EventInfo event{};
    event.event_id = 1;
    event.evse_id = evse_id;
    event.error = error;
    event.event_cleared = cleared;
    return event;
}

void expect_same_error_info(const ocpp::v16::ErrorInfo& actual, const ocpp::v16::ErrorInfo& expected) {
    EXPECT_EQ(actual.uuid, expected.uuid);
    EXPECT_EQ(actual.error_code, expected.error_code);
    EXPECT_EQ(actual.is_fault, expected.is_fault);
    EXPECT_EQ(opt_str(actual.info), opt_str(expected.info));
    EXPECT_EQ(opt_str(actual.vendor_id), opt_str(expected.vendor_id));
    EXPECT_EQ(opt_str(actual.vendor_error_code), opt_str(expected.vendor_error_code));
    EXPECT_EQ(actual.timestamp.to_rfc3339(), expected.timestamp.to_rfc3339());
}

class ChargePointV16ErrorDispatch : public ChargePointV16ErrorTest {
protected:
    MockFunction<void(std::int32_t, const ocpp::v16::ErrorInfo&)> m_raised;
    MockFunction<void(std::int32_t, const std::string&)> m_cleared;

    void dispatch(const GenericChargePointInterface::EventInfo& event) {
        m_chargepoint.dispatch_error_event(event, m_raised.AsStdFunction(), m_cleared.AsStdFunction());
    }

    void expect_raised_with_converted_info(const Everest::error::Error& error) {
        ocpp::v16::ErrorInfo reported(std::string{}, ocpp::v16::ChargePointErrorCode::NoError, false);
        EXPECT_CALL(m_raised, Call(Eq(1), _)).WillOnce([&reported](auto, const auto& info) { reported = info; });
        EXPECT_CALL(m_cleared, Call).Times(0);

        dispatch(make_event(error, 1, false));

        expect_same_error_info(reported, m_chargepoint.convert_error(error));
    }
};

TEST_F(ChargePointV16ErrorDispatch, RaisedErrorIsReportedOnceWithConvertedInfo) {
    expect_raised_with_converted_info(make_error("evse_board_support/MREC2GroundFailure", "sensor reports fault"));
}

TEST_F(ChargePointV16ErrorDispatch, RaisedNonMrecErrorIsReportedToo) {
    expect_raised_with_converted_info(make_error("evse_board_support/VendorError", "msg", "sub"));
}

// a clear is identified by its uuid alone; the error is not converted again
TEST_F(ChargePointV16ErrorDispatch, ClearedErrorReportsOnlyTheUuid) {
    EXPECT_CALL(m_cleared, Call(1, std::string(ERROR_UUID))).Times(1);
    EXPECT_CALL(m_raised, Call).Times(0);

    dispatch(make_event(make_error("evse_board_support/MREC2GroundFailure"), 1, true));
}

TEST_F(ChargePointV16ErrorDispatch, EventWithoutErrorIsIgnored) {
    EXPECT_CALL(m_raised, Call).Times(0);
    EXPECT_CALL(m_cleared, Call).Times(0);

    for (const bool cleared : {false, true}) {
        GenericChargePointInterface::EventInfo event{};
        event.evse_id = 1;
        event.event_cleared = cleared;
        dispatch(event);
    }
}

// The EVerest EVSE id is passed on as the OCPP 1.6 connector id, without translation. The two only
// coincide with one connector per EVSE.
TEST_F(ChargePointV16ErrorDispatch, EvseIdIsPassedThroughUnchanged) {
    const auto error = make_error("evse_board_support/MREC2GroundFailure");
    for (const std::int32_t evse_id : {0, 1, 2}) {
        SCOPED_TRACE(evse_id);
        EXPECT_CALL(m_raised, Call(evse_id, _)).Times(1);
        EXPECT_CALL(m_cleared, Call(evse_id, std::string(ERROR_UUID))).Times(1);

        dispatch(make_event(error, evse_id, false));
        dispatch(make_event(error, evse_id, true));

        testing::Mock::VerifyAndClearExpectations(&m_raised);
        testing::Mock::VerifyAndClearExpectations(&m_cleared);
    }
}

TEST_F(ChargePointV16ErrorDispatch, OnEventThrowsWhenNotConfigured) {
    const auto event = make_event(make_error("evse_board_support/MREC2GroundFailure"), 1, false);
    EXPECT_THROW(m_chargepoint.on_event(event), GenericChargePointInterface::NotConfigured);
}

} // namespace
