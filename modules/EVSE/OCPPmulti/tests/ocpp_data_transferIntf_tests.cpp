// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// cmds:
//   data_transfer:

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;

TEST_F(GenericOcppRequiresTester, callDataTransfer) {
    // call_data_transfer() used in cb_data_transfer()

    using ocpp::v2::DataTransferRequest;
    using ocpp::v2::DataTransferResponse;

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_data_transfer", "call_data_transfer",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    DataTransferRequest request;
    request.vendorId = "Pionix";
    request.messageId = "123456789";
    request.data = R"({"request_certificate_update":"now"})"_json;
    // std::optional<CustomData> customData;

    // DataTransferResponse:
    //   description: Type for data transfer response provided by OCPP
    //   type: object
    //   additionalProperties: false
    //   required:
    //     - status
    //   properties:
    //     status:
    //       description: Status of the data transfer
    //       type: string
    //       $ref: /ocpp#/DataTransferStatus
    //     data:
    //       description: Data provided by this data transfer
    //       type: string
    //     custom_data:
    //       description: Custom data extension
    //       type: object
    //       $ref: /ocpp#/CustomData
    //
    // DataTransferStatus:
    //   description: Data Transfer Status enum
    //   type: string
    //   enum:
    //     - Accepted
    //     - Rejected
    //     - UnknownMessageId
    //     - UnknownVendorId
    //     - Offline

    // Note Offline maps to UnknownVendorId in to_ocpp_data_transfer_status_enum()

    // data is a string
    const json cmd_response = R"({"status": "Accepted", "data": "{\"extraStatus\":\"Starting\"}" })"_json;
    // data is a JSON object
    const json expected_response = R"({"status": "Accepted", "data":{"extraStatus": "Starting"}})"_json;

    interfaces->add_cmd_result(cmd_response);
    const auto response = ocpp->cb_data_transfer(request);

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(
        received[0],
        R"({"request":{"data":"{\"request_certificate_update\":\"now\"}","message_id":"123456789","vendor_id":"Pionix"}})"_json);

    EXPECT_EQ(response, expected_response);
}

} // namespace
