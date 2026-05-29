// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <optional>
#include <string>
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

// 1:1 with types::iso15118::DcEvBPTParameters
struct BptParams {
    float discharge_max_current_limit; // [A]
    float discharge_max_power_limit;   // [W]
    float discharge_target_current;    // [A]
    float discharge_minimal_soc;       // [%]
};

// presence flag — selects EnergyTransferMode::MCS{_BPT}
struct McsProfile {};

struct CurvePoint {
    int32_t t_offset_ms; // absolute offset from session start
    float current_a;     // commanded current at this point
    bool three_phases;
    std::optional<int32_t> ramp_ms; // if set, linearly ramp from current
                                    // value to current_a over ramp_ms;
                                    // otherwise instantaneous step
};

struct ChargingCurve {
    std::vector<CurvePoint> points; // monotonically increasing t_offset_ms
    bool loop{false};               // repeat curve from t=0 after last point
};

struct StartSessionParams {
    ChargeMode mode;
    std::optional<PaymentOption> payment;
    std::optional<int32_t> departure_time_s;
    std::optional<int32_t> e_amount_wh;
    std::optional<float> charging_current_a;
    std::optional<bool> three_phases;
    std::optional<BptParams> bpt;
    std::optional<McsProfile> mcs;
    std::optional<ChargingCurve> curve;
};

struct SetChargingCurrentParams {
    float current_a;
    bool three_phases;
    std::optional<int32_t> ramp_ms;
};

struct SetSocParams {
    float soc_pct;
};

struct BcbToggleParams {
    std::optional<int32_t> count;
};

struct InjectFaultParams {
    FaultType type;
    std::optional<float> rcd_mA;
};

struct RunScenarioParams {
    ScenarioName name;
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
    std::string event;
};

struct SlacState {
    std::string state;
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
