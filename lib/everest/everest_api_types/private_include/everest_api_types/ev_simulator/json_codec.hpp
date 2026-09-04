// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include "nlohmann/json_fwd.hpp"
#include <everest_api_types/ev_simulator/API.hpp>

namespace everest::lib::API::V1_0::types::ev_simulator {

using json = nlohmann::json;

void to_json(json& j, FsmState const& k);
void from_json(const json& j, FsmState& k);

void to_json(json& j, ChargeMode const& k);
void from_json(const json& j, ChargeMode& k);

void to_json(json& j, PaymentOption const& k);
void from_json(const json& j, PaymentOption& k);

void to_json(json& j, FaultType const& k);
void from_json(const json& j, FaultType& k);

void to_json(json& j, ScenarioName const& k);
void from_json(const json& j, ScenarioName& k);

void to_json(json& j, IsoSessionEventKind const& k);
void from_json(const json& j, IsoSessionEventKind& k);

void to_json(json& j, CommandAckStatus const& k);
void from_json(const json& j, CommandAckStatus& k);

void to_json(json& j, BspEventKind const& k);
void from_json(const json& j, BspEventKind& k);

void to_json(json& j, SlacStateKind const& k);
void from_json(const json& j, SlacStateKind& k);

void to_json(json& j, BptParams const& k) noexcept;
void from_json(const json& j, BptParams& k);

void to_json(json& j, CurvePoint const& k) noexcept;
void from_json(const json& j, CurvePoint& k);

void to_json(json& j, ChargingCurve const& k) noexcept;
void from_json(const json& j, ChargingCurve& k);

void to_json(json& j, SessionConfigParams const& k) noexcept;
void from_json(const json& j, SessionConfigParams& k);

void to_json(json& j, SetChargingCurrentParams const& k) noexcept;
void from_json(const json& j, SetChargingCurrentParams& k);

void to_json(json& j, SetSocParams const& k) noexcept;
void from_json(const json& j, SetSocParams& k);

void to_json(json& j, BcbToggleParams const& k) noexcept;
void from_json(const json& j, BcbToggleParams& k);

void to_json(json& j, InjectFaultParams const& k) noexcept;
void from_json(const json& j, InjectFaultParams& k);

void to_json(json& j, ScenarioTimingOverrides const& k) noexcept;
void from_json(const json& j, ScenarioTimingOverrides& k);

void to_json(json& j, RunScenarioParams const& k) noexcept;
void from_json(const json& j, RunScenarioParams& k);

void to_json(json& j, EvInfo const& k) noexcept;
void from_json(const json& j, EvInfo& k);

void to_json(json& j, IsoSessionEvent const& k) noexcept;
void from_json(const json& j, IsoSessionEvent& k);

void to_json(json& j, BspEvent const& k) noexcept;
void from_json(const json& j, BspEvent& k);

void to_json(json& j, SlacState const& k) noexcept;
void from_json(const json& j, SlacState& k);

void to_json(json& j, FaultReport const& k) noexcept;
void from_json(const json& j, FaultReport& k);

void to_json(json& j, CommandAck const& k) noexcept;
void from_json(const json& j, CommandAck& k);

} // namespace everest::lib::API::V1_0::types::ev_simulator
