// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../main/ScenarioRunner.hpp"
#include "../main/PilotGate.hpp"
#include "../main/WorldFaults.hpp"

#include <catch2/catch_test_macros.hpp>
#include <everest_api_types/ev_driver_sim/codec.hpp>
#include <limits>
#include <nlohmann/json.hpp>

namespace {
namespace api = everest::lib::API::V1_0::types::ev_driver_sim;
using namespace std::chrono_literals;
using Runner = module::ScenarioRunner;

struct Fixture {
    Runner runner;
    module::PilotGate pilot{false};
    module::WorldFaults faults;
    std::vector<Runner::Command> commands;
    std::vector<api::ScenarioEvent> events;
    std::vector<module::PilotGate::Out> pilot_out;
    std::vector<module::WorldFaults::Out> fault_out;
    Runner::Actions actions{
        [this](const auto& name, const auto& payload) -> std::optional<std::string> {
            commands.push_back({name, payload});
            if (name == "plug") {
                if (pilot.passed()) {
                    return "already plugged";
                }
                pilot_out.push_back(pilot.plug());
            } else if (name == "unplug") {
                if (!pilot.passed()) {
                    return "already unplugged";
                }
                faults.unplug();
                pilot_out.push_back(pilot.unplug());
            } else if (name == "inject_fault") {
                fault_out.push_back(faults.inject(api::deserialize<api::InjectFaultParams>(payload), pilot.passed()));
                return fault_out.back().rejection;
            } else if (name == "clear_fault") {
                fault_out.push_back(faults.clear());
            }
            return std::nullopt;
        },
        [this](const auto& event) { events.push_back(event); }};
    Runner::Clock::time_point epoch{};

    void fire(int ms) {
        runner.on_timer(actions, epoch + std::chrono::milliseconds(ms));
    }
};

struct Preset {
    api::ScenarioName name;
    const char* mode;
    std::vector<int> times;
    std::vector<std::string> steps;
};
const std::vector<Preset> presets{
    {api::ScenarioName::AcIecBasic, "AcIec", {30000}, {"stop_session"}},
    {api::ScenarioName::AcIsoBasic, "AcIso2", {60000}, {"stop_session"}},
    {api::ScenarioName::DcIsoBasic, "DcIso2", {}, {}},
    {api::ScenarioName::AcIecPauseResume,
     "AcIec",
     {30000, 60000, 120000, 125000},
     {"pause_session", "resume_session", "stop_session", "unplug"}},
    {api::ScenarioName::DcIsoPauseResume,
     "DcIso2",
     {45000, 75000, 180000, 185000},
     {"pause_session", "resume_session", "stop_session", "unplug"}},
    {api::ScenarioName::AcIsoD20Basic, "AcIsoD20", {120000, 125000}, {"stop_session", "unplug"}},
    {api::ScenarioName::DcIsoD20Basic, "DcIsoD20", {180000, 185000}, {"stop_session", "unplug"}},
    {api::ScenarioName::DcIsoBpt, "DcIsoD20", {180000, 185000}, {"stop_session", "unplug"}},
    {api::ScenarioName::DcIsoMcs, "DcIsoD20", {180000, 185000}, {"stop_session", "unplug"}},
    {api::ScenarioName::DiodeFailSmoke, "AcIec", {15000, 20000}, {"inject_fault", "clear_fault"}},
    {api::ScenarioName::AcIecRampUp, "AcIec", {60000, 65000}, {"stop_session", "unplug"}},
    {api::ScenarioName::DcIsoTaper, "DcIso2", {120000, 125000}, {"stop_session", "unplug"}},
};

template <class T> bool decodes(const std::string& value) {
    T result{};
    return api::adl_deserialize(value, result);
}
} // namespace

TEST_CASE("EvDriverSim preserves every preset's steps and deadlines", "[evdriver][scenario]") {
    for (const auto& preset : presets) {
        CAPTURE(preset.name);
        Fixture f;
        REQUIRE_FALSE(f.runner.start({preset.name, {}}, true, f.pilot.passed(), f.actions, f.epoch));
        f.runner.configuration_ack(std::nullopt, f.actions, f.epoch);
        REQUIRE(f.commands.size() == 2);
        CHECK(f.commands[0].name == "configure_session");
        CHECK(f.commands[1].name == "plug");
        CHECK(f.pilot.passed());
        REQUIRE(f.pilot_out.size() == 1);
        CHECK(f.pilot_out.front().to_car_events.front().event == types::board_support_common::Event::A);
        const auto config = nlohmann::json::parse(f.commands[0].payload);
        CHECK(config.at("mode") == preset.mode);
        if (preset.name == api::ScenarioName::DcIsoBpt) {
            CHECK(config.at("params").at("bpt") == nlohmann::json{{"discharge_max_current_limit", 50.0},
                                                                  {"discharge_max_power_limit", 11000.0},
                                                                  {"discharge_target_current", 30.0},
                                                                  {"discharge_minimal_soc", 20.0}});
        }
        if (preset.name == api::ScenarioName::DcIsoMcs) {
            CHECK(config.at("params").at("mcs_enabled") == true);
        }
        if (preset.name == api::ScenarioName::AcIecRampUp || preset.name == api::ScenarioName::DcIsoTaper) {
            const auto& curve = config.at("params").at("curve");
            CHECK(curve.at("loop") == false);
            const auto& points = curve.at("points");
            if (preset.name == api::ScenarioName::AcIecRampUp) {
                CHECK(points == nlohmann::json::parse(R"([
                    {"t_offset_ms":2000,"current_a":8,"three_phases":true,"ramp_ms":2000},
                    {"t_offset_ms":8000,"current_a":16,"three_phases":true,"ramp_ms":2000},
                    {"t_offset_ms":20000,"current_a":32,"three_phases":true,"ramp_ms":4000}])"));
            } else {
                CHECK(points == nlohmann::json::parse(R"([
                    {"t_offset_ms":0,"current_a":100,"three_phases":false},
                    {"t_offset_ms":30000,"current_a":50,"three_phases":false,"ramp_ms":10000},
                    {"t_offset_ms":60000,"current_a":20,"three_phases":false,"ramp_ms":10000},
                    {"t_offset_ms":90000,"current_a":5,"three_phases":false,"ramp_ms":5000}])"));
            }
        }
        for (std::size_t i = 0; i < preset.times.size(); ++i) {
            REQUIRE(f.runner.next_deadline() == f.epoch + std::chrono::milliseconds(preset.times[i]));
            f.fire(preset.times[i] - 1);
            CHECK(f.commands.size() == i + 2);
            f.fire(preset.times[i]);
            REQUIRE(f.commands.size() == i + 3);
            CHECK(f.commands.back().name == preset.steps[i]);
            if (preset.steps[i] == "inject_fault") {
                CHECK(f.faults.active() == api::WorldFault::DiodeFail);
                CHECK(f.fault_out.back().diode_fail == true);
            } else if (preset.steps[i] == "clear_fault") {
                CHECK_FALSE(f.faults.active());
                CHECK(f.fault_out.back().diode_fail == false);
            } else if (preset.steps[i] == "unplug") {
                CHECK_FALSE(f.pilot.passed());
                CHECK(f.pilot_out.back().to_board_cp == types::ev_board_support::EvCpState::A);
            }
        }
        REQUIRE(f.events.size() == f.commands.size() + 2);
        CHECK(f.events.front().kind == api::ScenarioEventKind::Started);
        CHECK(f.events.back().kind == api::ScenarioEventKind::Finished);
        for (std::size_t i = 0; i < f.commands.size(); ++i) {
            CHECK(f.events[i + 1].name == preset.name);
            CHECK(f.events[i + 1].kind == api::ScenarioEventKind::StepFired);
            CHECK(f.events[i + 1].message == f.commands[i].name);
        }
        CHECK_FALSE(f.runner.active());
        f.fire(300000);
        CHECK(f.events.size() == f.commands.size() + 2);
    }
}

TEST_CASE("EvDriverSim timing overrides preserve validation and default phases", "[evdriver][scenario]") {
    Fixture f;
    api::ScenarioTimingOverrides timing;
    SECTION("all pause resume phases use overrides and late callbacks drain in order") {
        timing.pause_at_ms = 10;
        timing.resume_at_ms = 20;
        timing.stop_after_ms = 30;
        timing.unplug_after_ms = 40;
        REQUIRE_FALSE(
            f.runner.start({api::ScenarioName::AcIecPauseResume, timing}, true, f.pilot.passed(), f.actions, f.epoch));
        f.runner.configuration_ack(std::nullopt, f.actions, f.epoch);
        CHECK(f.runner.next_deadline() == f.epoch + 10ms);
        f.fire(25);
        REQUIRE(f.commands.size() == 4);
        CHECK(f.commands[2].name == "pause_session");
        CHECK(f.commands[3].name == "resume_session");
        CHECK(f.runner.next_deadline() == f.epoch + 30ms);
        f.fire(40);
        CHECK_FALSE(f.runner.active());
        CHECK_FALSE(f.pilot.passed());
    }
    SECTION("omitted phases retain defaults") {
        timing.stop_after_ms = 500;
        REQUIRE_FALSE(
            f.runner.start({api::ScenarioName::DcIsoD20Basic, timing}, true, f.pilot.passed(), f.actions, f.epoch));
        f.runner.configuration_ack(std::nullopt, f.actions, f.epoch);
        f.fire(500);
        CHECK(f.commands.back().name == "stop_session");
        CHECK(f.runner.next_deadline() == f.epoch + 185000ms);
    }
    SECTION("fault phases use overrides") {
        timing.fault_at_ms = 10;
        timing.clear_fault_at_ms = 20;
        REQUIRE_FALSE(
            f.runner.start({api::ScenarioName::DiodeFailSmoke, timing}, true, f.pilot.passed(), f.actions, f.epoch));
        f.runner.configuration_ack(std::nullopt, f.actions, f.epoch);
        f.fire(10);
        CHECK(f.faults.active() == api::WorldFault::DiodeFail);
        f.fire(20);
        CHECK_FALSE(f.faults.active());
    }
    SECTION("nonpositive, nonmonotonic and unused phases reject without effects") {
        for (const auto& payload : {R"({"name":"AcIecBasic","timing":{"stop_after_ms":0}})",
                                    R"({"name":"AcIecBasic","timing":{"stop_after_ms":-1}})",
                                    R"({"name":"AcIecBasic","timing":{"pause_at_ms":10}})",
                                    R"({"name":"AcIecPauseResume","timing":{"pause_at_ms":60000}})",
                                    R"({"name":"DiodeFailSmoke","timing":{"fault_at_ms":21000}})",
                                    R"({"name":"DcIsoBasic","timing":{"stop_after_ms":10}})"}) {
            const auto params = api::deserialize<api::RunScenarioParams>(payload);
            CHECK(f.runner.start(params, true, f.pilot.passed(), f.actions, f.epoch).has_value());
            CHECK(f.events.back().kind == api::ScenarioEventKind::Rejected);
            CHECK(f.events.back().message.has_value());
            CHECK(f.commands.empty());
            CHECK_FALSE(f.runner.active());
        }
    }
}

TEST_CASE("EvDriverSim scenario gates preserve the running preset", "[evdriver][scenario]") {
    Fixture f;
    CHECK(f.runner.start({api::ScenarioName::AcIecBasic, {}}, false, f.pilot.passed(), f.actions, f.epoch).has_value());
    CHECK(f.commands.empty());
    REQUIRE_FALSE(f.runner.start({api::ScenarioName::AcIecBasic, {}}, true, f.pilot.passed(), f.actions, f.epoch));
    f.runner.configuration_ack(std::nullopt, f.actions, f.epoch);
    CHECK(f.runner.start({api::ScenarioName::DcIsoBasic, {}}, true, f.pilot.passed(), f.actions, f.epoch) ==
          "scenario already running");
    CHECK(f.runner.next_deadline() == f.epoch + 30000ms);
    f.fire(30000);
    CHECK(f.commands.back().name == "stop_session");
    CHECK(f.events.back().name == api::ScenarioName::AcIecBasic);
    CHECK(f.runner.start({api::ScenarioName::DcIsoBasic, {}}, true, f.pilot.passed(), f.actions, f.epoch) ==
          "already plugged");
    f.pilot.unplug();
    REQUIRE_FALSE(f.runner.start({api::ScenarioName::DcIsoBasic, {}}, true, f.pilot.passed(), f.actions, f.epoch));
    f.runner.configuration_ack(std::nullopt, f.actions, f.epoch);
    CHECK_FALSE(f.runner.active());
}

TEST_CASE("EvDriverSim rejects unknown scenario names and malformed payloads", "[evdriver][scenario]") {
    Fixture f;
    const auto invalid = static_cast<api::ScenarioName>(9999);
    CHECK(f.runner.start({invalid, {}}, true, f.pilot.passed(), f.actions, f.epoch) == "unknown scenario name");
    CHECK(f.commands.empty());
    CHECK_THROWS_AS(api::serialize(invalid), std::out_of_range);
    for (const auto& payload :
         {"", "{", "{}", R"({"name":"NoSuchScenario"})", R"({"name":"AcIecBasic","timing":{"stop_after_ms":"bad"}})"}) {
        CHECK_FALSE(decodes<api::RunScenarioParams>(payload));
    }
}
TEST_CASE("ScenarioTimingOverrides codec preserves field values across round-trip", "[evdriver][command_router]") {
    SECTION("all timing fields decode to their exact values") {
        const std::string with_all_timing = R"({
            "name": "AcIecBasic",
            "timing": {
                "pause_at_ms": 1000,
                "resume_at_ms": 3000,
                "stop_after_ms": 5000,
                "unplug_after_ms": 6000,
                "fault_at_ms": 2000,
                "clear_fault_at_ms": 4000
            }
        })";
        CHECK(decodes<api::RunScenarioParams>(with_all_timing));

        api::RunScenarioParams params_full{};
        REQUIRE(api::adl_deserialize(with_all_timing, params_full));
        REQUIRE(params_full.timing.has_value());
        CHECK(params_full.timing->pause_at_ms == 1000);
        CHECK(params_full.timing->resume_at_ms == 3000);
        CHECK(params_full.timing->stop_after_ms == 5000);
        CHECK(params_full.timing->unplug_after_ms == 6000);
        CHECK(params_full.timing->fault_at_ms == 2000);
        CHECK(params_full.timing->clear_fault_at_ms == 4000);

        // Round-trip serialize -> deserialize preserves set and unset fields.
        const std::string serialized = api::serialize(params_full);
        auto params_rt = api::deserialize<api::RunScenarioParams>(serialized);
        REQUIRE(params_rt.timing.has_value());
        CHECK(params_rt.timing->pause_at_ms == 1000);
        CHECK(params_rt.timing->resume_at_ms == 3000);
        CHECK(params_rt.timing->stop_after_ms == 5000);
        CHECK(params_rt.timing->unplug_after_ms == 6000);
        CHECK(params_rt.timing->fault_at_ms == 2000);
        CHECK(params_rt.timing->clear_fault_at_ms == 4000);
    }

    SECTION("absent timing decodes to nullopt") {
        api::RunScenarioParams params_no_timing{};
        REQUIRE(api::adl_deserialize("{\"name\":\"AcIecBasic\"}", params_no_timing));
        CHECK_FALSE(params_no_timing.timing.has_value());
    }

    SECTION("partial timing leaves unset fields nullopt") {
        const std::string with_partial_timing = R"({"name":"DcIsoBasic","timing":{"stop_after_ms":8000}})";
        api::RunScenarioParams params_partial{};
        REQUIRE(api::adl_deserialize(with_partial_timing, params_partial));
        REQUIRE(params_partial.timing.has_value());
        CHECK_FALSE(params_partial.timing->pause_at_ms.has_value());
        CHECK(params_partial.timing->stop_after_ms == 8000);
    }

    SECTION("ScenarioTimingOverrides round-trips in isolation") {
        api::ScenarioTimingOverrides timing_only{};
        timing_only.pause_at_ms = 500;
        timing_only.fault_at_ms = 1500;
        const std::string timing_serialized = api::serialize(timing_only);
        auto timing_rt = api::deserialize<api::ScenarioTimingOverrides>(timing_serialized);
        CHECK(timing_rt.pause_at_ms == 500);
        CHECK_FALSE(timing_rt.resume_at_ms.has_value());
        CHECK(timing_rt.fault_at_ms == 1500);
        CHECK_FALSE(timing_rt.clear_fault_at_ms.has_value());
    }

    SECTION("all-nullopt ScenarioTimingOverrides stays all-nullopt across round-trip") {
        api::ScenarioTimingOverrides empty_overrides{};
        const std::string serialized = api::serialize(empty_overrides);
        auto rt = api::deserialize<api::ScenarioTimingOverrides>(serialized);
        CHECK_FALSE(rt.pause_at_ms.has_value());
        CHECK_FALSE(rt.resume_at_ms.has_value());
        CHECK_FALSE(rt.stop_after_ms.has_value());
        CHECK_FALSE(rt.unplug_after_ms.has_value());
        CHECK_FALSE(rt.fault_at_ms.has_value());
        CHECK_FALSE(rt.clear_fault_at_ms.has_value());
    }
}

TEST_CASE("ScenarioRunner replaces the acknowledgement deadline with offsets from acceptance", "[evdriver][scenario]") {
    Fixture f;
    REQUIRE_FALSE(f.runner.start({api::ScenarioName::AcIecPauseResume, {}}, true, false, f.actions, f.epoch));
    CHECK(f.runner.active());
    CHECK(f.runner.next_deadline() == f.epoch + 10s);
    f.fire(9999);
    CHECK_FALSE(f.pilot.passed());
    CHECK(f.events.empty());
    f.runner.configuration_ack(std::nullopt, f.actions, f.epoch + 9999ms);
    CHECK(f.runner.next_deadline() == f.epoch + 39999ms);
    f.fire(39998);
    CHECK(f.commands.size() == 2);
    f.fire(39999);
    CHECK(f.commands.back().name == "pause_session");
}

TEST_CASE("ScenarioRunner does not accept configuration at or after its deadline", "[evdriver][scenario]") {
    Fixture f;
    REQUIRE_FALSE(f.runner.start({api::ScenarioName::DcIsoBasic, {}}, true, false, f.actions, f.epoch));
    SECTION("timer expires") {
        f.fire(10000);
    }
    SECTION("acknowledgement wins the callback race at the deadline") {
        f.runner.configuration_ack(std::nullopt, f.actions, f.epoch + 10s);
    }
    CHECK_FALSE(f.runner.active());
    CHECK_FALSE(f.runner.next_deadline());
    CHECK_FALSE(f.pilot.passed());
    REQUIRE(f.events.size() == 1);
    CHECK(f.events.back().kind == api::ScenarioEventKind::Rejected);
    CHECK(f.events.back().message == "timed out waiting for EvController configure_session acknowledgement");
    f.runner.configuration_ack(std::nullopt, f.actions, f.epoch + 11s);
    f.fire(300000);
    CHECK(f.commands.size() == 1);
    CHECK(f.events.size() == 1);
}
