// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace everest::lib::API::V1_0::types::ev_simulator {

enum class FsmState {
    Disabled,
    Unplugged,
    Plugged,
    SlacMatching,
    V2GNegotiating,
    BcbToggling,
    Charging,
    ChargingPwmPaused,
    Paused,
    Stopping,
    Faulted,
};

enum class ChargeMode {
    AcIec,
    AcIso2,
    AcIsoD20,
    DcIso2,
    DcIsoD20,
};

enum class PaymentOption {
    ExternalPayment,
    Contract,
};

enum class FaultType {
    DiodeFail,
    RcdError,
    CpErrorE,
    SlacTimeout,
    V2GTimeout,
    Internal,
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

enum class IsoSessionEventKind {
    V2GStarted,
    PowerReady,
    StopFromCharger,
    PauseFromCharger,
    V2GFinished,
    DcPowerOn,
};

enum class CommandAckStatus {
    Accepted,
    Rejected,
};

// Mirrors values of internal ::types::board_support_common::Event.
enum class BspEventKind {
    A,
    B,
    C,
    D,
    E,
    F,
    PowerOn,
    PowerOff,
    Disconnected,
};

// Mirrors values of internal ::types::slac::State.
enum class SlacStateKind {
    Unmatched,
    Matching,
    Matched,
};

// 1:1 with types::iso15118::DcEvBPTParameters
struct BptParams {
    float discharge_max_current_limit; // [A]
    float discharge_max_power_limit;   // [W]
    float discharge_target_current;    // [A]
    float discharge_minimal_soc;       // [%]
};

struct CurvePoint {
    int32_t t_offset_ms; // offset from when the curve started playing
                         // (rebased on loop), not from session start
    float current_a;     // commanded current at this point
    bool three_phases;
    std::optional<int32_t> ramp_ms; // if set, linearly ramp from current
                                    // value to current_a over ramp_ms;
                                    // otherwise instantaneous step
};

struct ChargingCurve {
    std::vector<CurvePoint> points; // monotonically increasing t_offset_ms
    bool loop{false};               // repeat curve from t=0 after last point

    // Validating factory: returns nullopt when points is empty or t_offset_ms
    // is not strictly monotonic. The public constructor stays unvalidated so
    // direct aggregate initialization remains possible; this factory is the
    // recommended entry point and is used by from_json.
    static std::optional<ChargingCurve> make(std::vector<CurvePoint> points, bool loop);
};

// SessionConfigParams is a mode-tagged variant: each alternative carries only
// the fields the FSM actually consumes for that charge mode, so an illegal
// field combination (e.g. mcs_enabled on AcIec, bpt on a non-D20 mode) is
// not representable in C++ once parsed. Note this is a property of the
// post-parse type, not of the JSON boundary: from_json dispatches on `mode`
// and reads only that alternative's keys, so foreign keys present in the
// incoming JSON are dropped at parse time (neither stored nor rejected).

// AcIec drives a PWM-only session with no ISO handshake, so it carries no
// payment / departure / e_amount / bpt / mcs fields.
struct AcIecSessionParams {
    std::optional<float> charging_current_a;
    std::optional<bool> three_phases;
    std::optional<ChargingCurve> curve;
};

struct AcIso2SessionParams {
    std::optional<PaymentOption> payment;
    std::optional<int32_t> departure_time_s;
    std::optional<int32_t> e_amount_wh;
    std::optional<float> charging_current_a;
    std::optional<bool> three_phases;
    std::optional<ChargingCurve> curve;
};

// AcIsoD20 == AcIso2 plus optional BPT (AC_BPT EnergyTransferMode).
struct AcIsoD20SessionParams {
    std::optional<PaymentOption> payment;
    std::optional<int32_t> departure_time_s;
    std::optional<int32_t> e_amount_wh;
    std::optional<float> charging_current_a;
    std::optional<bool> three_phases;
    std::optional<BptParams> bpt;
    std::optional<ChargingCurve> curve;
};

// DC modes have no AC current / phase fields (SoC tracks delivered DC power).
struct DcIso2SessionParams {
    std::optional<PaymentOption> payment;
    std::optional<int32_t> departure_time_s;
    std::optional<int32_t> e_amount_wh;
    std::optional<ChargingCurve> curve;
};

// DcIsoD20 == DcIso2 plus optional BPT and the MCS selector. mcs_enabled only
// exists on this alternative, so "MCS on a non-DcIsoD20 mode" cannot occur.
struct DcIsoD20SessionParams {
    std::optional<PaymentOption> payment;
    std::optional<int32_t> departure_time_s;
    std::optional<int32_t> e_amount_wh;
    std::optional<BptParams> bpt;
    bool mcs_enabled{false};
    std::optional<ChargingCurve> curve;
};

using SessionConfigParams = std::variant<AcIecSessionParams, AcIso2SessionParams, AcIsoD20SessionParams,
                                         DcIso2SessionParams, DcIsoD20SessionParams>;

// Recovers the ChargeMode tag from the active alternative. Exhaustive visitor
// with no catch-all: adding an alternative without a mapping is a compile
// error, mirroring kind_of(Event).
ChargeMode mode_of(const SessionConfigParams& p);

struct SetChargingCurrentParams {
    float current_a;
    bool three_phases;
    std::optional<int32_t> ramp_ms;
};

struct SetSocParams {
    float soc_pct;
};

struct BcbToggleParams {
    // Number of B<->C round-trips to perform. Each round-trip is two CP edges
    // (B->C, then C->B). When unset, the FSM uses its default of 3 round-trips
    // (6 edges), matching the ISO 15118 BCB toggle pattern.
    std::optional<int32_t> count;
};

struct InjectFaultParams {
    FaultType type;
    std::optional<float> rcd_mA;
    // Optional descriptive message. Externally injected faults (the
    // inject_fault MQTT command) leave this unset. States that synthesize a
    // fault internally (SlacMatching/V2GNegotiating on a peer precondition
    // failure) carry the descriptive text here so it travels with the event
    // instead of being pre-seeded into FsmContext::vars.last_fault, where an
    // intervening transition could leave it stale for a later Faulted entry.
    std::optional<std::string> message;
};

// Per-invocation overrides for the named phase offsets of a scenario preset.
// Each field, when set, replaces that phase's default offset (ms from
// scenario start). Fields naming a phase the chosen preset does not use are
// rejected, not ignored. (Enforced by the dispatcher in WS-B; the codec
// layer accepts any field combination.)
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

struct EvInfo {
    float soc_pct;
    float battery_capacity_wh;
    float battery_charge_wh;
    float target_current_a;
    float target_voltage_v;
};

struct IsoSessionEvent {
    IsoSessionEventKind kind;
    std::optional<float> dc_voltage_v;
    std::optional<float> dc_current_a;
};

struct BspEvent {
    BspEventKind event;
};

struct SlacState {
    SlacStateKind state;
};

struct FaultReport {
    FaultType type;
    std::optional<std::string> message;
    std::optional<float> rcd_mA;
};

struct CommandAck {
    std::string command;
    CommandAckStatus status;
    std::optional<std::string> reason;
};

} // namespace everest::lib::API::V1_0::types::ev_simulator
