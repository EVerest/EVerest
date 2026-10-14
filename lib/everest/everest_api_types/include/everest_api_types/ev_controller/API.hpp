// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace everest::lib::API::V1_0::types::ev_controller {

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
    ChargerFault,
};

enum class ChargeMode {
    AcIec,
    AcIso2,
    AcIsoD20,
    AcIsoD20DerIec,
    AcIsoD20DerSae,
    DcIso2,
    DcIsoD20,
};

enum class DerFlavor {
    IEC,
    SAE,
};

enum class PaymentOption {
    ExternalPayment,
    Contract,
};

enum class FaultType {
    SlacTimeout,
    V2GTimeout,
    Internal,
    ChargerFault,
};

enum class IsoSessionEventKind {
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

struct AcIsoD20DerParams {
    std::optional<PaymentOption> payment;
    std::optional<int32_t> departure_time_s;
    std::optional<int32_t> e_amount_wh;
    std::optional<float> charging_current_a;
    std::optional<bool> three_phases;
    std::optional<ChargingCurve> curve;
};

struct AcIsoD20DerIecSessionParams : AcIsoD20DerParams {};
struct AcIsoD20DerSaeSessionParams : AcIsoD20DerParams {};

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

using SessionConfigParams =
    std::variant<AcIecSessionParams, AcIso2SessionParams, AcIsoD20SessionParams, DcIso2SessionParams,
                 DcIsoD20SessionParams, AcIsoD20DerIecSessionParams, AcIsoD20DerSaeSessionParams>;

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

// Per-field override of the present values the EV reports over ISO 15118.
// EvController otherwise echoes what the EVSE delivered, so this is how a test
// makes the EV report a value the charger did not deliver and reaches the
// SECC obligations that only a disagreement triggers.
//
// A field left unset keeps echoing. Overriding one value therefore cannot
// silently zero the other, which matters because a present value the EV never
// reports is transmitted as zero and reads at the SECC as a measured zero.
struct SetPresentValuesParams {
    std::optional<float> present_voltage;      // [V] overrides the delivered DC voltage
    std::optional<float> present_active_power; // [W] overrides the integrated active power
};

// Teardown selector for the stop_session command. `abort` terminates the V2G
// session immediately instead of exchanging SessionStop, which is how the
// suite stages a malformed teardown; the default is the clean stop.
struct StopSessionParams {
    bool abort{false};
};

struct BcbToggleParams {
    // Number of B<->C round-trips to perform. Each round-trip is two CP edges
    // (B->C, then C->B). When unset, the FSM uses its default of 3 round-trips
    // (6 edges), matching the ISO 15118 BCB toggle pattern.
    std::optional<int32_t> count;
};

enum class DerControlSource {
    ChargeParameterDiscovery,
    ChargeLoop,
};

struct DerNegotiatedFunctions {
    DerFlavor flavor;
    std::vector<std::string> functions;
};

struct DerControl {
    DerFlavor flavor;
    DerControlSource source;
    std::optional<bool> permit_service;
    std::optional<std::vector<std::string>> enabled_functions;
    std::optional<std::vector<std::string>> problems;
    std::optional<float> dso_q_setpoint;
    std::optional<float> dso_cos_phi_setpoint;
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

} // namespace everest::lib::API::V1_0::types::ev_controller
