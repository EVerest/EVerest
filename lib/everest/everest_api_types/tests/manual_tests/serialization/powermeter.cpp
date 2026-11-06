// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "everest_api_types/powermeter/codec.hpp"
#include "everest_api_types/utilities/codec.hpp"
#include <gtest/gtest.h>

using namespace everest::lib::API::V1_0::types::powermeter;
using everest::lib::API::deserialize;

TEST(powermeter, values_need_numbers) {
    PowermeterValues values;
    EXPECT_TRUE(deserialize(R"({"timestamp": "2024-09-27T07:21:14.127Z", "energy_Wh_import": {"total": 5}})", values));
    EXPECT_FALSE(
        deserialize(R"({"timestamp": "2024-09-27T07:21:14.127Z", "energy_Wh_import": {"total": true}})", values));
    EXPECT_FALSE(deserialize(R"({"timestamp": "2024-09-27T07:21:14.127Z", "energy_Wh_import": {"total": 5},
                                 "voltage_V": {"DC": false}})",
                             values));
    EXPECT_FALSE(deserialize(R"({"timestamp": "2024-09-27T07:21:14.127Z", "energy_Wh_import": {"total": 5},
                                 "temperatures": [{"temperature": "40"}]})",
                             values));
}

TEST(powermeter, capabilities_reject_unknown_properties) {
    Capabilities caps;
    EXPECT_TRUE(deserialize(R"({})", caps));
    EXPECT_TRUE(deserialize(R"({"min_import_current_A": 10, "min_export_current_A": 5})", caps));
    EXPECT_FALSE(deserialize(R"({"min_import_current_A": 10, "foo": 1})", caps));
    EXPECT_FALSE(deserialize(R"({"min_import_current_A": true})", caps));
}

TEST(powermeter, stop_reply_signed_meter_value_is_optional) {
    ReplyStopTransaction reply;
    EXPECT_TRUE(deserialize(R"({"status": "OK"})", reply));
    EXPECT_TRUE(deserialize(R"({"status": "OK", "signed_meter_value": {"signed_meter_data": "d", "signing_method": "m",
                                "encoding_method": "OCMF", "timestamp": "2024-09-27T07:21:14Z"}})",
                            reply));
    EXPECT_TRUE(deserialize(R"({"status": "NOT_SUPPORTED"})", reply));
    EXPECT_TRUE(deserialize(R"({"status": "UNEXPECTED_ERROR", "error": "e"})", reply));
}
