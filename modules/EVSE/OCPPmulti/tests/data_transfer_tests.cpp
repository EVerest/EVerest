// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;
using ocpp::v2::DataTransferStatusEnum;
using ::testing::_;
using ::testing::Return;
using types::ocpp::DataTransferRequest;
using types::ocpp::DataTransferResponse;
using types::ocpp::DataTransferStatus;

TEST_F(GenericOcppProvidesTester, dataTransferAccepted) {
    DataTransferRequest request{"Pionix", "12345678", "{}"};

    ocpp::v2::DataTransferRequest expected_request;
    expected_request.vendorId = "Pionix";
    expected_request.messageId = "12345678";
    expected_request.data = "{}"_json;
    ocpp::v2::DataTransferResponse set_return;
    set_return.status = DataTransferStatusEnum::Accepted;
    set_return.data = R"({"emergencyACLimit": 32})"_json;
    EXPECT_CALL(chargepoint, data_transfer_req(expected_request)).WillOnce(Return(set_return));

    const auto result = ocpp->handle_data_transfer(request);
    EXPECT_EQ(result.status, DataTransferStatus::Accepted);
    ASSERT_TRUE(result.data.has_value());
    EXPECT_EQ(result.data.value(), "{\"emergencyACLimit\":32}");
}

TEST_F(GenericOcppProvidesTester, dataTransferRejected) {
    DataTransferRequest request{"Pionix", "12345678", "{}"};

    ocpp::v2::DataTransferRequest expected_request;
    expected_request.vendorId = "Pionix";
    expected_request.messageId = "12345678";
    expected_request.data = "{}"_json;
    ocpp::v2::DataTransferResponse set_return;
    set_return.status = DataTransferStatusEnum::Rejected;
    set_return.data = R"({"emergencyACLimit": 32})"_json;
    EXPECT_CALL(chargepoint, data_transfer_req(expected_request)).WillOnce(Return(set_return));

    const auto result = ocpp->handle_data_transfer(request);
    EXPECT_EQ(result.status, DataTransferStatus::Rejected);
    ASSERT_TRUE(result.data.has_value());
    EXPECT_EQ(result.data.value(), "{\"emergencyACLimit\":32}");
}

TEST_F(GenericOcppProvidesTester, dataTransferOfflineNoResponse) {
    DataTransferRequest request{"Pionix", "12345678", "{}"};

    ocpp::v2::DataTransferRequest expected_request;
    expected_request.vendorId = "Pionix";
    expected_request.messageId = "12345678";
    expected_request.data = "{}"_json;
    EXPECT_CALL(chargepoint, data_transfer_req(expected_request)).WillOnce(Return(std::nullopt));

    const auto result = ocpp->handle_data_transfer(request);
    EXPECT_EQ(result.status, DataTransferStatus::Offline);
    EXPECT_FALSE(result.data.has_value());
}

TEST(GenericOcppProvides, dataTransferOffline) {
    // called before run - so should be rejected

    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    stubs::ModuleInterfaces interfaces;

    // connect required interfaces
    interfaces.add_charger_information("info");
    interfaces.add_data_transfer("data_transfer");
    interfaces.add_display_message("display");
    interfaces.add_evse_energy_sink("energy_node", 1);
    interfaces.add_evse_manager("evse_manager_1");
    interfaces.add_evse_manager("evse_manager_2");
    interfaces.add_extensions_15118("evsev2g");
    interfaces.add_reservation("reservation");

    chargepoint.load_store("default_store.json");

    // GenericOcpp object
    stubs::GenericOcppTester ocpp(chargepoint, interfaces.get_module_info(), config, interfaces.get_provides(),
                                  interfaces.get_requires());

    ocpp.init();

    DataTransferRequest request{"Pionix", "12345678", "{}"};

    ocpp::v2::DataTransferRequest expected_request;
    expected_request.vendorId = "Pionix";
    expected_request.messageId = "12345678";
    expected_request.data = "{}"_json;
    EXPECT_CALL(chargepoint, data_transfer_req(expected_request)).Times(0);

    const auto result = ocpp.handle_data_transfer(request);
    EXPECT_EQ(result.status, DataTransferStatus::Offline);
    EXPECT_FALSE(result.data.has_value());
}

} // namespace
