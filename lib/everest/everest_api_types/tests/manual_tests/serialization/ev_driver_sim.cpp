// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "SerializationTestHelpers.hpp"
#include "everest_api_types/ev_controller/codec.hpp"
#include "everest_api_types/ev_driver_sim/API.hpp"
#include "everest_api_types/ev_driver_sim/codec.hpp"
#include "nlohmann/json.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <utility>

using namespace everest::lib::API::V1_0::types::ev_driver_sim;

TEST(ev_driver_sim, WorldFault_roundtrip) {
    for (const auto& [value, spelling] : {
             std::pair{WorldFault::DiodeFail, "DiodeFail"},
             std::pair{WorldFault::RcdError, "RcdError"},
             std::pair{WorldFault::CpErrorE, "CpErrorE"},
         }) {
        SCOPED_TRACE(spelling);
        const auto encoded = serialize(value);
        EXPECT_EQ(nlohmann::json::parse(encoded), spelling);
        const auto decoded = try_deserialize<WorldFault>(encoded);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(*decoded, value);
        std::ostringstream stream;
        stream << value;
        EXPECT_EQ(stream.str(), encoded);
    }
}

TEST(ev_driver_sim, PilotState_roundtrip) {
    for (const auto& [value, spelling] : {
             std::pair{PilotState::Withheld, "Withheld"},
             std::pair{PilotState::Passed, "Passed"},
         }) {
        SCOPED_TRACE(spelling);
        const auto encoded = serialize(value);
        EXPECT_EQ(nlohmann::json::parse(encoded), spelling);
        const auto decoded = try_deserialize<PilotState>(encoded);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(*decoded, value);
        std::ostringstream stream;
        stream << value;
        EXPECT_EQ(stream.str(), encoded);
    }
}

TEST(ev_driver_sim, CommandAckStatus_roundtrip) {
    for (const auto& [value, spelling] : {
             std::pair{CommandAckStatus::Accepted, "Accepted"},
             std::pair{CommandAckStatus::Rejected, "Rejected"},
         }) {
        SCOPED_TRACE(spelling);
        const auto encoded = serialize(value);
        EXPECT_EQ(nlohmann::json::parse(encoded), spelling);
        const auto decoded = try_deserialize<CommandAckStatus>(encoded);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(*decoded, value);
        std::ostringstream stream;
        stream << value;
        EXPECT_EQ(stream.str(), encoded);
    }
}

TEST(ev_driver_sim, InjectFaultParams_roundtrip) {
    for (const auto& original : {InjectFaultParams{WorldFault::DiodeFail, {}, {}},
                                 InjectFaultParams{WorldFault::RcdError, 30.5f, "leakage"}}) {
        const auto decoded = codec_test(original);
        EXPECT_EQ(decoded.type, original.type);
        EXPECT_EQ(decoded.rcd_mA, original.rcd_mA);
        EXPECT_EQ(decoded.message, original.message);
    }
    EXPECT_EQ(nlohmann::json::parse(serialize(InjectFaultParams{WorldFault::DiodeFail, {}, {}})),
              nlohmann::json::parse(R"({"type":"DiodeFail"})"));
    EXPECT_EQ(nlohmann::json::parse(serialize(InjectFaultParams{WorldFault::RcdError, 30.5f, "leakage"})),
              nlohmann::json::parse(R"({"type":"RcdError","rcd_mA":30.5,"message":"leakage"})"));
}

TEST(ev_driver_sim, WorldState_roundtrip) {
    for (const auto& original :
         {WorldState{PilotState::Withheld, {}}, WorldState{PilotState::Passed, WorldFault::CpErrorE}}) {
        const auto decoded = codec_test(original);
        EXPECT_EQ(decoded.pilot, original.pilot);
        EXPECT_EQ(decoded.fault, original.fault);
    }
    EXPECT_EQ(nlohmann::json::parse(serialize(WorldState{PilotState::Withheld, {}})),
              nlohmann::json::parse(R"({"pilot":"Withheld"})"));
    EXPECT_EQ(nlohmann::json::parse(serialize(WorldState{PilotState::Passed, WorldFault::CpErrorE})),
              nlohmann::json::parse(R"({"pilot":"Passed","fault":"CpErrorE"})"));
}

TEST(ev_driver_sim, CommandAck_roundtrip) {
    for (const auto& original : {CommandAck{"plug", CommandAckStatus::Accepted, {}},
                                 CommandAck{"run_scenario", CommandAckStatus::Rejected, "busy"}}) {
        const auto decoded = codec_test(original);
        EXPECT_EQ(decoded.command, original.command);
        EXPECT_EQ(decoded.status, original.status);
        EXPECT_EQ(decoded.message, original.message);
    }
    EXPECT_EQ(nlohmann::json::parse(serialize(CommandAck{"plug", CommandAckStatus::Accepted, {}})),
              nlohmann::json::parse(R"({"command":"plug","status":"Accepted"})"));
    EXPECT_EQ(nlohmann::json::parse(serialize(CommandAck{"run_scenario", CommandAckStatus::Rejected, "busy"})),
              nlohmann::json::parse(R"({"command":"run_scenario","status":"Rejected","reason":"busy"})"));
}

TEST(ev_driver_sim, WorldFault_malformed_rejected) {
    EXPECT_FALSE(try_deserialize<WorldFault>(R"("SlacTimeout")"));
    EXPECT_FALSE(try_deserialize<WorldFault>("42"));
}

TEST(ev_driver_sim, PilotState_malformed_rejected) {
    EXPECT_FALSE(try_deserialize<PilotState>(R"("unknown")"));
    EXPECT_FALSE(try_deserialize<PilotState>("true"));
}

TEST(ev_driver_sim, CommandAckStatus_malformed_rejected) {
    EXPECT_FALSE(try_deserialize<CommandAckStatus>(R"("unknown")"));
    EXPECT_FALSE(try_deserialize<CommandAckStatus>("[]"));
}

TEST(ev_driver_sim, InjectFaultParams_malformed_rejected) {
    EXPECT_FALSE(try_deserialize<InjectFaultParams>("{}"));
    EXPECT_FALSE(try_deserialize<InjectFaultParams>(R"({"type":"SlacTimeout"})"));
    EXPECT_FALSE(try_deserialize<InjectFaultParams>(R"({"type":"RcdError","rcd_mA":"thirty"})"));
    EXPECT_FALSE(try_deserialize<InjectFaultParams>(R"({"type":"DiodeFail","message":false})"));
}

TEST(ev_driver_sim, WorldState_malformed_rejected) {
    EXPECT_FALSE(try_deserialize<WorldState>("{}"));
    EXPECT_FALSE(try_deserialize<WorldState>(R"({"pilot":"unknown"})"));
    EXPECT_FALSE(try_deserialize<WorldState>(R"({"pilot":"Passed","fault":"Internal"})"));
}

TEST(ev_driver_sim, CommandAck_malformed_rejected) {
    EXPECT_FALSE(try_deserialize<CommandAck>(R"({"command":"plug"})"));
    EXPECT_FALSE(try_deserialize<CommandAck>(R"({"status":"Accepted"})"));
    EXPECT_FALSE(try_deserialize<CommandAck>(R"({"command":"plug","status":"unknown"})"));
    EXPECT_FALSE(try_deserialize<CommandAck>(R"({"command":"plug","status":"Rejected","reason":12})"));
}

TEST(ev_driver_sim, CommandAck_payloads_match_ev_controller) {
    namespace controller = everest::lib::API::V1_0::types::ev_controller;
    for (const auto& original :
         {controller::CommandAck{"plug", controller::CommandAckStatus::Accepted, {}},
          controller::CommandAck{"run_scenario", controller::CommandAckStatus::Rejected, "busy"}}) {
        const auto payload = controller::serialize(original);
        const auto decoded = try_deserialize<CommandAck>(payload);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded->message, original.reason);
        const auto encoded = serialize(*decoded);
        EXPECT_EQ(nlohmann::json::parse(encoded), nlohmann::json::parse(payload));
        const auto restored = controller::try_deserialize<controller::CommandAck>(encoded);
        ASSERT_TRUE(restored);
        EXPECT_EQ(restored->command, original.command);
        EXPECT_EQ(restored->status, original.status);
        EXPECT_EQ(restored->reason, original.reason);
    }
}

TEST(ev_driver_sim, ScenarioName_roundtrip) {
    for (const auto name : {ScenarioName::AcIecBasic, ScenarioName::AcIecPauseResume, ScenarioName::AcIsoBasic,
                            ScenarioName::AcIsoD20Basic, ScenarioName::DcIsoBasic, ScenarioName::DcIsoD20Basic,
                            ScenarioName::DcIsoPauseResume, ScenarioName::DcIsoBpt, ScenarioName::DcIsoMcs,
                            ScenarioName::DiodeFailSmoke, ScenarioName::AcIecRampUp, ScenarioName::DcIsoTaper}) {
        EXPECT_EQ(codec_test(name), name);
    }
    EXPECT_FALSE(try_deserialize<ScenarioName>(R"("unknown")"));
}

TEST(ev_driver_sim, ScenarioTimingOverrides_roundtrip) {
    for (const auto& original : {ScenarioTimingOverrides{}, ScenarioTimingOverrides{10, 20, 30, 40, 50, 60}}) {
        const auto decoded = codec_test(original);
        EXPECT_EQ(decoded.pause_at_ms, original.pause_at_ms);
        EXPECT_EQ(decoded.resume_at_ms, original.resume_at_ms);
        EXPECT_EQ(decoded.stop_after_ms, original.stop_after_ms);
        EXPECT_EQ(decoded.unplug_after_ms, original.unplug_after_ms);
        EXPECT_EQ(decoded.fault_at_ms, original.fault_at_ms);
        EXPECT_EQ(decoded.clear_fault_at_ms, original.clear_fault_at_ms);
    }
}

TEST(ev_driver_sim, RunScenarioParams_roundtrip) {
    const auto minimal = codec_test(RunScenarioParams{ScenarioName::AcIecBasic, {}});
    EXPECT_EQ(minimal.name, ScenarioName::AcIecBasic);
    EXPECT_FALSE(minimal.timing);
    const auto decoded =
        codec_test(RunScenarioParams{ScenarioName::DcIsoD20Basic, ScenarioTimingOverrides{10, 20, 30, 40, 50, 60}});
    ASSERT_TRUE(decoded.timing);
    EXPECT_EQ(decoded.timing->stop_after_ms, 30);
    EXPECT_EQ(nlohmann::json::parse(serialize(RunScenarioParams{ScenarioName::AcIecBasic, {}})),
              nlohmann::json::parse(R"({"name":"AcIecBasic"})"));
    EXPECT_FALSE(try_deserialize<RunScenarioParams>("{}"));
    EXPECT_FALSE(try_deserialize<RunScenarioParams>(R"({"name":"unknown"})"));
}

TEST(ev_driver_sim, ScenarioEvent_roundtrip) {
    for (const auto& [kind, spelling] :
         {std::pair{ScenarioEventKind::Started, "Started"}, std::pair{ScenarioEventKind::StepFired, "StepFired"},
          std::pair{ScenarioEventKind::Finished, "Finished"}, std::pair{ScenarioEventKind::Rejected, "Rejected"}}) {
        EXPECT_EQ(codec_test(kind), kind);
        EXPECT_EQ(nlohmann::json::parse(serialize(kind)), spelling);
        for (const auto& message : {std::optional<std::string>{}, std::optional<std::string>{"test message"}}) {
            const ScenarioEvent original{ScenarioName::DcIsoBpt, kind, message};
            const auto decoded = codec_test(original);
            EXPECT_EQ(decoded.name, original.name);
            EXPECT_EQ(decoded.kind, original.kind);
            EXPECT_EQ(decoded.message, original.message);
            auto expected = nlohmann::json{{"name", "DcIsoBpt"}, {"kind", spelling}};
            if (message) {
                expected["message"] = *message;
            }
            EXPECT_EQ(nlohmann::json::parse(serialize(original)), expected);
        }
    }
    EXPECT_FALSE(try_deserialize<ScenarioEventKind>(R"("unknown")"));
    EXPECT_FALSE(try_deserialize<ScenarioEvent>(R"({"name":"AcIecBasic"})"));
    EXPECT_FALSE(try_deserialize<ScenarioEvent>(R"({"name":"AcIecBasic","kind":"unknown"})"));
}
