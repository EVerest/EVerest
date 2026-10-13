// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace everest::lib::API::V1_0::types::ev_driver_sim {

enum class WorldFault {
    DiodeFail,
    RcdError,
    CpErrorE,
};

enum class PilotState {
    Withheld,
    Passed,
};

enum class CommandAckStatus {
    Accepted,
    Rejected,
};

struct InjectFaultParams {
    WorldFault type;
    std::optional<float> rcd_mA;
    std::optional<std::string> message;
};

struct WorldState {
    PilotState pilot;
    std::optional<WorldFault> fault;
};

struct CommandAck {
    std::string command;
    CommandAckStatus status;
    std::optional<std::string> message; // JSON key "reason", shared with ev_controller acknowledgements.
};

enum class ScenarioName {
    AcIecBasic,
    AcIecPauseResume,
    AcIsoBasic,
    AcIsoD20Basic,
    DcIsoBasic,
    DcIsoD20Basic,
    DcIsoPauseResume,
    DcIsoBpt,
    DcIsoMcs,
    DiodeFailSmoke,
    AcIecRampUp,
    DcIsoTaper,
};

struct ScenarioTimingOverrides {
    std::optional<int32_t> pause_at_ms;
    std::optional<int32_t> resume_at_ms;
    std::optional<int32_t> stop_after_ms;
    std::optional<int32_t> unplug_after_ms;
    std::optional<int32_t> fault_at_ms;
    std::optional<int32_t> clear_fault_at_ms;
};

struct RunScenarioParams {
    ScenarioName name;
    std::optional<ScenarioTimingOverrides> timing;
};

enum class ScenarioEventKind {
    Started,
    StepFired,
    Finished,
    Rejected,
};

struct ScenarioEvent {
    ScenarioName name;
    ScenarioEventKind kind;
    std::optional<std::string> message;
};

} // namespace everest::lib::API::V1_0::types::ev_driver_sim
