// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest_api_types/ev_simulator/API.hpp>
#include <generated/types/board_support_common.hpp>
#include <generated/types/evse_manager.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace module {

namespace API_types = everest::lib::API::V1_0::types;

// `kind` is redundant with `payload.index()` but is preserved as the switch
// driver: producers set `kind` and `payload` together, consumers `switch` on
// `kind` for readability. The two MUST agree by construction.
enum class EventKind {
    // External commands
    Enable,
    Disable,
    Plug,
    Unplug,
    SetSoc,
    StartSession,
    StopSession,
    PauseSession,
    ResumeSession,
    SetChargingCurrent,
    InjectFault,
    ClearFault,
    BcbToggle,
    RunScenario,
    QueryState, // pull-model state discovery for late subscribers
    // Peer subscription forwards
    BspEvent,
    BspMeasurement,
    EvInfo,
    SlacState,
    IsoPowerReady,
    IsoAcMaxCurrent,
    IsoAcTargetPower,
    IsoStopFromCharger,
    IsoV2GFinished,
    IsoDcPowerOn,
    IsoPauseFromCharger,
    // Internal
    StateDeadline,
    Shutdown,
};

// Payload-bearing forwards mirror peer interface payloads via the typed API.
struct BspEventPayload {
    ::types::board_support_common::BspEvent bsp_event;
};

struct BspMeasurementPayload {
    float cp_pwm_duty_cycle;
    std::optional<float> rcd_current_mA;
    ::types::board_support_common::ProximityPilot proximity_pilot;
};

struct EvInfoPayload {
    ::types::evse_manager::EVInfo ev_info;
};

struct SlacStatePayload {
    std::string state;
};

struct IsoSessionPayload {
    API_types::ev_simulator::IsoSessionEventKind kind;
    std::optional<float> dc_voltage_v;
    std::optional<float> dc_current_a;
};

struct ErrorPayload {
    std::string type;
    std::string message;
};

struct Event {
    EventKind kind;
    std::variant<std::monostate, API_types::ev_simulator::StartSessionParams, API_types::ev_simulator::SetSocParams,
                 API_types::ev_simulator::SetChargingCurrentParams, API_types::ev_simulator::BcbToggleParams,
                 API_types::ev_simulator::InjectFaultParams, API_types::ev_simulator::RunScenarioParams,
                 BspEventPayload, BspMeasurementPayload, EvInfoPayload, SlacStatePayload, IsoSessionPayload,
                 ErrorPayload>
        payload{};
};

} // namespace module
