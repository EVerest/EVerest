// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "everest_api_types/evse_manager/codec.hpp"
#include "everest_api_types/utilities/codec.hpp"
#include <gtest/gtest.h>

#include <string>

using namespace everest::lib::API::V1_0::types::evse_manager;
using everest::lib::API::deserialize;

namespace {
std::string request(const std::string& connector_id, const std::string& priority) {
    return R"({"connector_id": )" + connector_id +
           R"(, "source": {"enable_source": "LocalAPI", "enable_state": "Disable", "enable_priority": )" + priority +
           "}}";
}
} // namespace

TEST(evse_manager, enable_disable_priority_is_an_integer_from_0_to_10000) {
    EnableDisableRequest req;
    EXPECT_TRUE(deserialize(request("1", "0"), req));
    EXPECT_TRUE(deserialize(request("1", "10000"), req));
    EXPECT_FALSE(deserialize(request("1", "10001"), req));
    EXPECT_FALSE(deserialize(request("1", "-1"), req));
    EXPECT_FALSE(deserialize(request("1", "true"), req));
    EXPECT_FALSE(deserialize(request("1", "5.5"), req));
    EXPECT_FALSE(deserialize(request("true", "5"), req));
}

TEST(evse_manager, enable_disable_rejects_numbers_that_do_not_fit_int32) {
    EnableDisableRequest req;
    // 2^32 + 1 and 2^32 + 5 would wrap to connector 1 and priority 5
    EXPECT_FALSE(deserialize(request("4294967297", "5"), req));
    EXPECT_FALSE(deserialize(request("1", "4294967301"), req));
    EXPECT_FALSE(deserialize(request("-2147483649", "5"), req));
    EXPECT_TRUE(deserialize(request("2147483647", "5"), req));
    EXPECT_EQ(req.connector_id, 2147483647);
}
