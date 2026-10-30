// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "everest_api_types/energy/codec.hpp"
#include "everest_api_types/utilities/codec.hpp"
#include <gtest/gtest.h>

#include <string>

using namespace everest::lib::API::V1_0::types::energy;
using everest::lib::API::deserialize;

namespace {
std::string limits_with(const std::string& entry) {
    return R"({"schedule_import": [)" + entry + R"(], "schedule_export": [], "schedule_setpoints": []})";
}
} // namespace

TEST(energy, external_limits_enforce_numbers_and_integers) {
    ExternalLimits limits;
    EXPECT_TRUE(deserialize(limits_with(R"({"timestamp": "2026-10-28T13:00:00Z", "limits_to_leaves": {},
        "limits_to_root": {"ac_max_current_A": {"value": 10, "source": "x"}, "ac_max_phase_count": {"value": 1, "source": "x"}}})"),
                            limits));
    EXPECT_FALSE(deserialize(limits_with(R"({"timestamp": "2026-10-28T13:00:00Z", "limits_to_leaves": {},
        "limits_to_root": {"ac_max_current_A": {"value": true, "source": "x"}}})"),
                             limits));
    EXPECT_FALSE(deserialize(limits_with(R"({"timestamp": "2026-10-28T13:00:00Z", "limits_to_leaves": {},
        "limits_to_root": {"ac_max_phase_count": {"value": 1.5, "source": "x"}}})"),
                             limits));
}
