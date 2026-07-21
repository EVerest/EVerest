// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "AcTemperatureDerating.hpp"

#include <ModuleAdapterStub.hpp>
#include <generated/interfaces/external_energy_limits/Interface.hpp>
#include <generated/interfaces/temperature_sensor/Interface.hpp>
#include <generated/types/energy.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <vector>

namespace {
using namespace std::chrono_literals;

struct CapturingEnergyLimitsAdapter : module::stub::QuietModuleAdapterStub {
    std::vector<types::energy::ExternalLimits> published_limits;

    Result call_fn(const Requirement&, const std::string& cmd, Parameters parameters) override {
        if (cmd == "set_external_limits") {
            published_limits.push_back(parameters.at("value").get<types::energy::ExternalLimits>());
        }
        return {};
    }
};

std::optional<double> extract_ac_max_current_A(const types::energy::ExternalLimits& limits) {
    if (limits.schedule_import.empty()) {
        return std::nullopt;
    }
    const auto& ac_max = limits.schedule_import.front().limits_to_leaves.ac_max_current_A;
    if (!ac_max.has_value()) {
        return std::nullopt;
    }
    return ac_max->value;
}

std::unique_ptr<module::AcTemperatureDerating> make_module(CapturingEnergyLimitsAdapter& adapter,
                                                           module::Conf& config) {
    Requirement temperature_req;
    temperature_req.id = "temperature";
    temperature_req.index = 0;
    Requirement energy_node_req;
    energy_node_req.id = "energy_node";
    energy_node_req.index = 0;

    std::vector<std::unique_ptr<temperature_sensorIntf>> temperature_providers;
    temperature_providers.push_back(
        std::make_unique<temperature_sensorIntf>(&adapter, temperature_req, "sensor1", std::nullopt));

    auto energy_node =
        std::make_unique<external_energy_limitsIntf>(&adapter, energy_node_req, "energy_node", std::nullopt);

    ModuleInfo module_info{};
    module_info.id = "ac_temp_derating";
    module_info.name = "AcTemperatureDerating";

    return std::make_unique<module::AcTemperatureDerating>(module_info, std::move(temperature_providers),
                                                           std::move(energy_node), config);
}

module::Conf make_test_conf() {
    module::Conf config{};
    config.derating_curves_json =
        R"({"sensor1.Powermeter": [{"temp_C": 25, "max_current_A": 32}, {"temp_C": 55, "max_current_A": 6}]})";
    config.temperature_provider_ignore_list = "";
    config.fallback_max_current_A = 5.0;
    config.temperature_stale_timeout_ms = 10000;
    config.update_debounce_ms = 1000;
    return config;
}

} // namespace

TEST(AcTemperatureDeratingModuleTest, LimitDecreaseIsPublishedImmediately) {
    CapturingEnergyLimitsAdapter adapter;
    auto config = make_test_conf();
    auto module = make_module(adapter, config);
    module::AcTemperatureDerating::TestAccess::init(*module);

    module::AcTemperatureDerating::TestAccess::set_identified_reading(*module, 0, "Powermeter", 25.0, 0ms);
    module::AcTemperatureDerating::TestAccess::update_and_publish_limits(*module);
    ASSERT_EQ(adapter.published_limits.size(), 1U);
    ASSERT_TRUE(extract_ac_max_current_A(adapter.published_limits.front()).has_value());
    EXPECT_NEAR(extract_ac_max_current_A(adapter.published_limits.front()).value(), 32.0, 0.01);

    module::AcTemperatureDerating::TestAccess::set_identified_reading(*module, 0, "Powermeter", 55.0, 0ms);
    module::AcTemperatureDerating::TestAccess::update_and_publish_limits(*module);
    ASSERT_EQ(adapter.published_limits.size(), 2U);
    ASSERT_TRUE(extract_ac_max_current_A(adapter.published_limits.back()).has_value());
    EXPECT_NEAR(extract_ac_max_current_A(adapter.published_limits.back()).value(), 6.0, 0.01);
}

TEST(AcTemperatureDeratingModuleTest, LimitIncreaseIsDebounced) {
    CapturingEnergyLimitsAdapter adapter;
    auto config = make_test_conf();
    auto module = make_module(adapter, config);
    module::AcTemperatureDerating::TestAccess::init(*module);

    module::AcTemperatureDerating::TestAccess::set_identified_reading(*module, 0, "Powermeter", 55.0, 0ms);
    module::AcTemperatureDerating::TestAccess::update_and_publish_limits(*module);
    ASSERT_EQ(adapter.published_limits.size(), 1U);

    module::AcTemperatureDerating::TestAccess::set_identified_reading(*module, 0, "Powermeter", 25.0, 0ms);
    module::AcTemperatureDerating::TestAccess::update_and_publish_limits(*module);
    EXPECT_EQ(adapter.published_limits.size(), 1U);

    module::AcTemperatureDerating::TestAccess::set_last_publish_time_age(*module, 1001ms);
    module::AcTemperatureDerating::TestAccess::update_and_publish_limits(*module);
    ASSERT_EQ(adapter.published_limits.size(), 2U);
    ASSERT_TRUE(extract_ac_max_current_A(adapter.published_limits.back()).has_value());
    EXPECT_NEAR(extract_ac_max_current_A(adapter.published_limits.back()).value(), 32.0, 0.01);
}

TEST(AcTemperatureDeratingModuleTest, StaleReadingUsesFallbackLimit) {
    CapturingEnergyLimitsAdapter adapter;
    auto config = make_test_conf();
    auto module = make_module(adapter, config);
    module::AcTemperatureDerating::TestAccess::init(*module);

    module::AcTemperatureDerating::TestAccess::set_identified_reading(*module, 0, "Powermeter", 25.0, 0ms);
    module::AcTemperatureDerating::TestAccess::update_and_publish_limits(*module);
    ASSERT_EQ(adapter.published_limits.size(), 1U);

    module::AcTemperatureDerating::TestAccess::set_identified_reading(*module, 0, "Powermeter", 25.0, 10001ms);
    module::AcTemperatureDerating::TestAccess::update_and_publish_limits(*module);
    ASSERT_EQ(adapter.published_limits.size(), 2U);
    ASSERT_TRUE(extract_ac_max_current_A(adapter.published_limits.back()).has_value());
    EXPECT_NEAR(extract_ac_max_current_A(adapter.published_limits.back()).value(), config.fallback_max_current_A, 0.01);
}
