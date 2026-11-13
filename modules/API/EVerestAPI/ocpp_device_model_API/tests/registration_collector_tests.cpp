// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "registration_collector.hpp"

namespace {

using namespace std::chrono_literals;
using module::parse_owner_list;
using module::RegistrationCollector;
using Registration = RegistrationCollector::Registration;
using Clock = std::chrono::steady_clock;
namespace dm = everest::lib::API::V1_0::types::ocpp_device_model;

constexpr auto module_id = "device_model_api";
constexpr auto short_timeout = 100ms;
constexpr auto long_timeout = 5s;

Registration registration(std::optional<std::string> owner, std::string const& component = "Fan") {
    dm::RegisteredVariable variable;
    variable.variable.name = "Speed";
    variable.characteristics.data_type = dm::DataEnum::integer;
    variable.characteristics.supports_monitoring = false;
    variable.attributes.push_back({});

    dm::RegisteredComponent registered;
    registered.component.name = component;
    registered.variables.push_back(variable);

    Registration result;
    result.owner = std::move(owner);
    result.components.push_back(registered);
    return result;
}

std::vector<std::string> owners(RegistrationCollector::Answer const& answer) {
    EXPECT_EQ(answer.owners.size(), answer.registrations.size());
    return answer.owners;
}

std::string component_of(RegistrationCollector::Answer const& answer, std::string const& owner) {
    for (auto const& registration : answer.registrations) {
        if (registration.owner.value_or(module_id) == owner) {
            return registration.components.at(0).component.name;
        }
    }
    return {};
}

TEST(ParseOwnerList, trims_names_and_drops_empty_entries) {
    EXPECT_EQ(parse_owner_list("a, b,,c "), (std::set<std::string>{"a", "b", "c"}));
    EXPECT_EQ(parse_owner_list(" fan_1 ,\tfan_2"), (std::set<std::string>{"fan_1", "fan_2"}));
}

TEST(ParseOwnerList, empty_and_blank_lists_have_no_owners) {
    EXPECT_TRUE(parse_owner_list("").empty());
    EXPECT_TRUE(parse_owner_list(" , ,").empty());
}

TEST(RegistrationCollector, reports_the_expected_owners_sorted) {
    RegistrationCollector collector{module_id, "meter_2,meter_1", long_timeout, short_timeout};
    EXPECT_EQ(collector.expected_owners(), (std::vector<std::string>{"meter_1", "meter_2"}));
}

TEST(RegistrationCollector, registrations_before_collect_are_answered) {
    RegistrationCollector collector{module_id, "meter_1,meter_2", long_timeout, short_timeout};
    EXPECT_FALSE(collector.add(registration("meter_1")));
    EXPECT_FALSE(collector.add(registration("meter_2")));

    const auto start = Clock::now();
    const auto answer = collector.collect();
    EXPECT_LT(Clock::now() - start, 1s);
    EXPECT_EQ(owners(answer), (std::vector<std::string>{"meter_1", "meter_2"}));
    EXPECT_TRUE(answer.missing_owners.empty());
    EXPECT_TRUE(answer.unexpected_owners.empty());
}

TEST(RegistrationCollector, a_registration_without_owner_counts_as_the_module_id) {
    RegistrationCollector collector{module_id, module_id, long_timeout, short_timeout};
    collector.add(registration(std::nullopt));

    const auto answer = collector.collect();
    ASSERT_EQ(answer.registrations.size(), 1U);
    EXPECT_FALSE(answer.registrations.at(0).owner.has_value());
    EXPECT_EQ(answer.owners, (std::vector<std::string>{module_id}));
    EXPECT_TRUE(answer.missing_owners.empty());
}

TEST(RegistrationCollector, the_latest_registration_of_an_owner_replaces_the_earlier_one) {
    RegistrationCollector collector{module_id, "meter_1", long_timeout, short_timeout};
    collector.add(registration("meter_1", "Old"));
    collector.add(registration("meter_1", "New"));

    const auto answer = collector.collect();
    ASSERT_EQ(answer.registrations.size(), 1U);
    EXPECT_EQ(component_of(answer, "meter_1"), "New");
}

TEST(RegistrationCollector, an_empty_registration_removes_the_owner) {
    RegistrationCollector collector{module_id, "", long_timeout, 0ms};
    collector.add(registration("fan_1"));
    collector.add(registration("fan_2"));
    Registration release;
    release.owner = "fan_1";
    collector.add(release);

    EXPECT_EQ(owners(collector.collect()), (std::vector<std::string>{"fan_2"}));
}

TEST(RegistrationCollector, answers_as_soon_as_the_last_expected_owner_registers) {
    RegistrationCollector collector{module_id, "meter_1,meter_2", long_timeout, short_timeout};
    collector.add(registration("meter_1"));

    std::thread late([&collector] {
        std::this_thread::sleep_for(50ms);
        collector.add(registration("meter_2"));
    });
    const auto start = Clock::now();
    const auto answer = collector.collect();
    const auto waited = Clock::now() - start;
    late.join();

    EXPECT_GE(waited, 50ms);
    EXPECT_LT(waited, 2s);
    EXPECT_EQ(owners(answer), (std::vector<std::string>{"meter_1", "meter_2"}));
}

TEST(RegistrationCollector, answers_at_the_deadline_without_the_missing_owners) {
    RegistrationCollector collector{module_id, "meter_1,meter_2", short_timeout, long_timeout};
    collector.add(registration("meter_1"));

    const auto start = Clock::now();
    const auto answer = collector.collect();
    const auto waited = Clock::now() - start;

    EXPECT_GE(waited, short_timeout);
    EXPECT_LT(waited, 2s);
    EXPECT_EQ(owners(answer), (std::vector<std::string>{"meter_1"}));
    EXPECT_EQ(answer.missing_owners, (std::vector<std::string>{"meter_2"}));
}

TEST(RegistrationCollector, owners_outside_the_expected_list_are_included_and_reported) {
    RegistrationCollector collector{module_id, "meter_1", long_timeout, short_timeout};
    collector.add(registration("meter_1"));
    collector.add(registration("stranger"));

    const auto answer = collector.collect();
    EXPECT_EQ(owners(answer), (std::vector<std::string>{"meter_1", "stranger"}));
    EXPECT_EQ(answer.unexpected_owners, (std::vector<std::string>{"stranger"}));
}

TEST(RegistrationCollector, without_expected_owners_it_waits_the_full_registration_timeout) {
    RegistrationCollector collector{module_id, "", long_timeout, short_timeout};
    collector.add(registration("fan_1"));

    std::thread late([&collector] {
        std::this_thread::sleep_for(20ms);
        collector.add(registration("fan_2"));
    });
    const auto start = Clock::now();
    const auto answer = collector.collect();
    const auto waited = Clock::now() - start;
    late.join();

    EXPECT_GE(waited, short_timeout);
    EXPECT_LT(waited, 2s);
    EXPECT_EQ(owners(answer), (std::vector<std::string>{"fan_1", "fan_2"}));
    EXPECT_TRUE(answer.missing_owners.empty());
    EXPECT_TRUE(answer.unexpected_owners.empty());
}

TEST(RegistrationCollector, a_registration_timeout_of_zero_answers_at_once) {
    RegistrationCollector collector{module_id, "", long_timeout, 0ms};

    const auto start = Clock::now();
    const auto answer = collector.collect();
    EXPECT_LT(Clock::now() - start, 1s);
    EXPECT_TRUE(answer.registrations.empty());
}

TEST(RegistrationCollector, registrations_after_the_answer_are_passed_on) {
    RegistrationCollector collector{module_id, "", long_timeout, 0ms};
    EXPECT_FALSE(collector.add(registration("fan_1")));

    const auto answer = collector.collect();
    EXPECT_TRUE(collector.add(registration("fan_2")));
    EXPECT_EQ(owners(answer), (std::vector<std::string>{"fan_1"}));
}

TEST(RegistrationCollector, collecting_again_collects_instead_of_passing_on) {
    RegistrationCollector collector{module_id, "fan_2", long_timeout, short_timeout};
    collector.add(registration("fan_2"));
    collector.collect();
    Registration release;
    release.owner = "fan_2";
    EXPECT_TRUE(collector.add(release));
    EXPECT_TRUE(collector.add(registration("fan_1")));

    collector.start_collecting();
    // an answer to the prompt that arrives before collect() runs
    EXPECT_FALSE(collector.add(registration("fan_3")));

    bool passed_on = true;
    std::thread late([&collector, &passed_on] {
        std::this_thread::sleep_for(100ms);
        passed_on = collector.add(registration("fan_2"));
    });
    const auto answer = collector.collect();
    late.join();

    EXPECT_FALSE(passed_on);
    EXPECT_EQ(owners(answer), (std::vector<std::string>{"fan_1", "fan_2", "fan_3"}));
    EXPECT_TRUE(collector.add(registration("fan_4")));
}

TEST(RegistrationCollector, cancel_ends_the_wait_for_expected_owners) {
    RegistrationCollector collector{module_id, "meter_1,meter_2", long_timeout, short_timeout};
    collector.add(registration("meter_1"));

    std::thread canceller([&collector] {
        std::this_thread::sleep_for(50ms);
        collector.cancel();
    });
    const auto start = Clock::now();
    const auto answer = collector.collect();
    const auto waited = Clock::now() - start;
    canceller.join();

    EXPECT_LT(waited, 2s);
    EXPECT_EQ(owners(answer), (std::vector<std::string>{"meter_1"}));
    EXPECT_EQ(answer.missing_owners, (std::vector<std::string>{"meter_2"}));
}

TEST(RegistrationCollector, cancel_ends_the_registration_timeout) {
    RegistrationCollector collector{module_id, "", short_timeout, long_timeout};

    std::thread canceller([&collector] {
        std::this_thread::sleep_for(50ms);
        collector.cancel();
    });
    const auto start = Clock::now();
    collector.collect();
    const auto waited = Clock::now() - start;
    canceller.join();

    EXPECT_LT(waited, 2s);
}

TEST(RegistrationCollector, collect_after_cancel_does_not_wait) {
    RegistrationCollector collector{module_id, "meter_1", long_timeout, long_timeout};
    collector.cancel();

    const auto start = Clock::now();
    collector.collect();
    EXPECT_LT(Clock::now() - start, 2s);
}

} // namespace
