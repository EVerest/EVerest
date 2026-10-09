// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Pins the OCPP 2.x MREC error reporting. The expected values come from the hand-typed table in
// mrec_fixture.hpp, never from the production mapping tables.
//
// The chain under test is the production one: ChargePointV2::on_event builds the EventData and
// asks the real GenericOcpp (no custom error mapping file configured) for the techCode;
// the EventData handed to libocpp is captured and compared as a whole.

#include "mrec_fixture.hpp"
#include "stubs/chargepoint_stub.hpp"
#include "stubs/config_stub.hpp"
#include "stubs/generic_ocpp_stub.hpp"
#include "stubs/interfaces_stub.hpp"
#include "stubs/v2_chargepoint_stub.hpp"

#include <v2_chargepoint.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace {

using namespace ocpp_multi;
using testing::_;
using testing::NiceMock;
using testing::SaveArg;

constexpr std::int32_t EVENT_ID = 42;
constexpr const char* DESCRIPTION = "a description";

// expose the protected test seam
struct TestChargePointV2 : public ChargePointV2 {
    using ChargePointV2::ChargePointV2;
    using ChargePointV2::set_charge_point;
};

Everest::error::Error make_error(std::string_view type, const std::string& message, const std::string& sub_type,
                                 std::optional<Mapping> mapping) {
    Everest::error::Error error;
    error.type = std::string(type);
    error.sub_type = sub_type;
    error.message = message;
    error.description = DESCRIPTION;
    error.origin = ImplementationIdentifier("bsp_1", "main", mapping);
    error.vendor_id = "error-vendor";
    error.timestamp = date::utc_clock::now();
    error.uuid = Everest::error::UUID("mrec-error-uuid-1");
    return error;
}

// the NotifyEvent eventData for an MREC error, spelled out field by field
ocpp::v2::EventData expected_event_data(const Everest::error::Error& error, const mrec_fixture::Entry& entry,
                                        bool cleared) {
    ocpp::v2::EventData expected;
    expected.eventId = EVENT_ID;
    expected.timestamp = ocpp::DateTime(error.timestamp);
    expected.trigger = ocpp::v2::EventTriggerEnum::Alerting;
    expected.actualValue = cleared ? "false" : "true";
    if (error.origin.mapping.has_value()) {
        ocpp::v2::EVSE evse;
        evse.id = error.origin.mapping->evse;
        evse.connectorId = error.origin.mapping->connector;
        expected.component.name = "EVSE";
        expected.component.evse = evse;
    } else {
        expected.component.name = "ChargingStation";
    }
    expected.eventNotificationType = ocpp::v2::EventNotificationEnum::HardWiredNotification;
    expected.variable.name = "Problem";
    expected.techCode = std::string(entry.v2_tech_code);
    expected.techInfo = error.message.empty() ? error.description : error.message;
    expected.cleared = cleared;
    return expected;
}

class ChargePointV2MrecFreeze : public testing::Test {
protected:
    stubs::ChargePointStub generic_chargepoint;
    stubs::ConfigStub config; // CustomErrorMappingPath empty: built-in mappings only
    stubs::ModuleInterfaces interfaces;
    std::unique_ptr<stubs::GenericOcppTester> generic_ocpp;
    std::unique_ptr<TestChargePointV2> chargepoint;
    NiceMock<stubs::Ocpp2ChargePointMock>* libocpp{nullptr}; // owned by chargepoint

    void SetUp() override {
        interfaces.add_charger_information("info");
        interfaces.add_data_transfer("data_transfer");
        interfaces.add_display_message("display");
        interfaces.add_evse_energy_sink("energy_node", 1);
        interfaces.add_evse_manager("evse_manager_1");
        interfaces.add_evse_manager("evse_manager_2");
        interfaces.add_extensions_15118("evsev2g");
        interfaces.add_reservation("reservation");

        generic_ocpp =
            std::make_unique<stubs::GenericOcppTester>(generic_chargepoint, interfaces.get_module_info(), config,
                                                       interfaces.get_provides(), interfaces.get_requires());
        generic_ocpp->init();

        chargepoint = std::make_unique<TestChargePointV2>(*generic_ocpp, interfaces.r_security);
        auto mock = std::make_unique<NiceMock<stubs::Ocpp2ChargePointMock>>();
        libocpp = mock.get();
        chargepoint->set_charge_point(std::move(mock));
    }

    void TearDown() override {
        chargepoint.reset();
        generic_ocpp.reset();
    }

    ocpp::v2::EventData send(const Everest::error::Error& error, bool cleared) {
        std::vector<ocpp::v2::EventData> sent;
        EXPECT_CALL(*libocpp, on_event(_)).WillOnce(SaveArg<0>(&sent));

        GenericChargePointInterface::EventInfo event{};
        event.event_id = EVENT_ID;
        event.evse_id = error.origin.mapping.has_value() ? error.origin.mapping->evse : 0;
        event.error = error;
        event.event_cleared = cleared;
        chargepoint->on_event(event);

        testing::Mock::VerifyAndClearExpectations(libocpp);
        EXPECT_EQ(sent.size(), 1U);
        return sent.empty() ? ocpp::v2::EventData{} : sent.front();
    }

    void expect_all_entries(const std::string& message, const std::string& sub_type, std::optional<Mapping> mapping) {
        for (const auto& entry : mrec_fixture::ENTRIES) {
            for (const bool cleared : {false, true}) {
                SCOPED_TRACE(std::string(entry.type) + (cleared ? " cleared" : " raised"));
                const auto error = make_error(entry.type, message, sub_type, mapping);
                const nlohmann::json actual = send(error, cleared);
                const nlohmann::json expected = expected_event_data(error, entry, cleared);
                EXPECT_EQ(actual, expected);
            }
        }
    }
};

TEST_F(ChargePointV2MrecFreeze, MessageIsSentAsTechInfo) {
    expect_all_entries("sensor reports fault", "", Mapping(1, 1));
}

TEST_F(ChargePointV2MrecFreeze, EmptyMessageSendsDescriptionAsTechInfo) {
    expect_all_entries("", "", Mapping(1, 1));
}

TEST_F(ChargePointV2MrecFreeze, SubTypeDoesNotChangeTheResult) {
    expect_all_entries("sensor reports fault", "some_sub_type", Mapping(1, 1));
}

TEST_F(ChargePointV2MrecFreeze, EvseOnlyMappingReportsEvseComponent) {
    expect_all_entries("sensor reports fault", "", Mapping(2));
}

TEST_F(ChargePointV2MrecFreeze, NoMappingReportsChargingStationComponent) {
    expect_all_entries("sensor reports fault", "", std::nullopt);
}

// Counterpart of V16MrecMatchIsSubstring: on 2.x only the exact key is mapped, so a type
// that merely contains an MREC key is reported with its own type as techCode.
TEST_F(ChargePointV2MrecFreeze, V2MrecMatchIsExact) {
    const auto error = make_error("vendor/evse_manager/MREC5OverVoltage_ext", "", "", Mapping(1, 1));
    const auto sent = send(error, false);
    ASSERT_TRUE(sent.techCode.has_value());
    EXPECT_EQ(sent.techCode->get(), "vendor/evse_manager/MREC5OverVoltage_ext");
}

} // namespace
