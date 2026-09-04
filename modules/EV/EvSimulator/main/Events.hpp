// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest_api_types/ev_simulator/API.hpp>
#include <generated/types/board_support_common.hpp>
#include <generated/types/evse_manager.hpp>
#include <generated/types/iso15118.hpp>
#include <utils/error.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace module {

namespace API_types = everest::lib::API::V1_0::types;

// The variant alternative IS the event identity. There is no separate stored
// `kind` field, so a producer cannot construct an event whose tag disagrees
// with its payload — disagreement is structurally impossible. Consumers that
// want the readable grouped `switch` recover the discriminant via
// `kind_of(ev)`, which is the single place a `std::visit` over the variant
// lives.
enum class EventKind {
    // External commands
    Enable,
    Disable,
    Plug,
    Unplug,
    SetSoc,
    ConfigureSession,
    StopSession,
    PauseSession,
    ResumeSession,
    SetChargingCurrent,
    InjectFault,
    ClearFault,
    BcbToggle,
    RunScenario,
    RaiseError, // out-of-band error raise, routed onto the loop thread
    ClearError, // out-of-band error clear, routed onto the loop thread
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
    DcEvsePresentCurrent,
    DcEvsePresentVoltage,
    // Internal
    BeginSession, // Plugged::enter self-advance: consume latched config / default
    StateDeadline,
    Shutdown,
};

// Empty tag types for the events that carry no payload. Named `XxxCmd` for
// external commands and `XxxEvt` for peer/internal events.
struct EnableCmd {};
struct DisableCmd {};
struct PlugCmd {};
struct UnplugCmd {};
struct StopSessionCmd {};
struct PauseSessionCmd {};
struct ResumeSessionCmd {};
struct ClearFaultCmd {};
struct QueryStateCmd {};
struct IsoPowerReadyEvt {};
// AC EVSE-communicated limit forwards. Unlike the other ISO edge events
// these carry a payload: the most recent ac_evse_max_current value, and
// the ac_evse_target_power struct from which a per-phase current is
// derived. The FSM clamps the applied charge current against them.
struct IsoAcMaxCurrentEvt {
    float max_current_a{0.0f};
};
struct IsoAcTargetPowerEvt {
    ::types::iso15118::AcTargetPower target_power{};
};
struct IsoStopFromChargerEvt {};
struct IsoV2GFinishedEvt {};
struct IsoDcPowerOnEvt {};
struct IsoPauseFromChargerEvt {};
struct BeginSessionEvt {};
struct StateDeadlineEvt {};
struct ShutdownEvt {};

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

// DC live present-current / present-voltage forwards from the SECC. A producer
// publishing dc_evse_present_current / dc_evse_present_voltage on the
// ISO15118_ev requirement routes these into vars so the SoC integrator
// switches from open-loop fallback to closed-loop live current.
struct DcEvsePresentCurrentPayload {
    double current_a;
};

struct DcEvsePresentVoltagePayload {
    double voltage_v;
};

// Parsed `raise_error` / `clear_error` command args. The MQTT-thread command
// router decodes the payload and enqueues one of these so the actual
// p_ev_manager error interaction happens on the loop thread, which owns all
// peer/error publishing. Carrying the parsed fields (not the raw payload)
// keeps the loop-thread side free of any JSON decoding.
struct RaiseErrorCmd {
    std::string type;
    std::string sub_type;
    std::string message;
    Everest::error::Severity severity{Everest::error::Severity::High};
};

struct ClearErrorCmd {
    std::string type;
    std::optional<std::string> sub_type;
};

using EventPayload =
    std::variant<EnableCmd, DisableCmd, PlugCmd, UnplugCmd, API_types::ev_simulator::SetSocParams,
                 API_types::ev_simulator::SessionConfigParams, StopSessionCmd, PauseSessionCmd, ResumeSessionCmd,
                 API_types::ev_simulator::SetChargingCurrentParams, API_types::ev_simulator::InjectFaultParams,
                 ClearFaultCmd, API_types::ev_simulator::BcbToggleParams, API_types::ev_simulator::RunScenarioParams,
                 RaiseErrorCmd, ClearErrorCmd, QueryStateCmd, BspEventPayload, BspMeasurementPayload, EvInfoPayload,
                 SlacStatePayload, IsoPowerReadyEvt, IsoAcMaxCurrentEvt, IsoAcTargetPowerEvt, IsoStopFromChargerEvt,
                 IsoV2GFinishedEvt, IsoDcPowerOnEvt, IsoPauseFromChargerEvt, DcEvsePresentCurrentPayload,
                 DcEvsePresentVoltagePayload, BeginSessionEvt, StateDeadlineEvt, ShutdownEvt>;

// One variant alternative per EventKind value. If a future alternative is
// added without a matching EventKind (or vice versa) this fails to compile.
static_assert(std::variant_size_v<EventPayload> == 33,
              "EventPayload alternatives and EventKind values must stay in lockstep");

namespace detail {
// Maps an EventKind to the default-constructed payload alternative that
// represents it. Used only by the EventKind-taking Event constructor (a
// construction convenience); the alternative produced is always internally
// consistent with whatever kind_of() will later report, so this cannot create
// a tag/payload mismatch.
EventPayload default_payload_for(EventKind kind);
} // namespace detail

struct Event {
    EventPayload payload;

    Event() : payload(EnableCmd{}) {
    }

    // Implicit so producers can `enqueue(SomePayload{})` and write
    // `Event{SomePayload{}}`.
    Event(EventPayload p) : payload(std::move(p)) {
    }

    // Construct from a discriminant alone (payload-less events, and the
    // common test pattern `Event ev{EventKind::X}; ev.payload = P{};`).
    explicit Event(EventKind kind) : payload(detail::default_payload_for(kind)) {
    }
};

// Recovers the discriminant from the active variant alternative. This is the
// only std::visit over EventPayload; the visitor has an explicit overload per
// alternative and no generic catch-all, so adding an alternative without a
// mapping is a compile error.
EventKind kind_of(const Event& ev);

// The command-ack verb literal for an EventKind. Implemented as its own
// exhaustive switch so a newly added EventKind forces a verb decision at
// compile time (-Werror=switch). Returns the exact externally observable
// literal that the states' blanket-reject arms pass to
// publish_e2m_command_ack for that command; non-command kinds return "".
std::string_view command_verb(EventKind kind);

} // namespace module
