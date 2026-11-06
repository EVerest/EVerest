// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "everest_api_types/dc_external_derate/codec.hpp"
#include "everest_api_types/utilities/codec.hpp"
#include <gtest/gtest.h>

using namespace everest::lib::API::V1_0::types::dc_external_derate;
using everest::lib::API::deserialize;

TEST(dc_external_derate, only_an_object_of_known_limits_is_a_derating) {
    ExternalDerating derating;
    EXPECT_TRUE(deserialize(R"({})", derating));
    EXPECT_FALSE(derating.max_export_current_A.has_value());
    EXPECT_TRUE(deserialize(R"({"max_export_current_A": 10, "max_import_current_A": 5, "max_export_power_W": 4000,
                                "max_import_power_W": 2000})",
                            derating));
    EXPECT_FLOAT_EQ(derating.max_export_current_A.value(), 10.0f);
    for (const auto* payload :
         {"10", "null", "true", "[]", R"("x")", R"({"max_export_current_A": true})",
          R"({"max_export_current_A": "10"})", R"({"max_export_current_A": 10, "foo": 1})", R"({"foo": 1})"}) {
        ExternalDerating rejected;
        EXPECT_FALSE(deserialize(payload, rejected)) << payload;
    }
}

TEST(dc_external_derate, a_limit_beyond_float_range_is_not_infinity) {
    // 1e39 does not fit a float; converted, it became +inf, i.e. no limit at all
    ExternalDerating derating;
    EXPECT_FALSE(deserialize(R"({"max_export_current_A": 1e39})", derating));
    EXPECT_FALSE(deserialize(R"({"max_import_power_W": -1e39})", derating));
}
