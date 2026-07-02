// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "API.hpp"
#include <iostream>
#include <optional>
#include <string>
#include <typeinfo>

namespace everest::lib::API::V1_0::types::ev_simulator {

std::string serialize(FsmState val);
std::string serialize(ChargeMode val);
std::string serialize(PaymentOption val);
std::string serialize(FaultType val);
std::string serialize(ScenarioName val);
std::string serialize(IsoSessionEventKind val);
std::string serialize(CommandAckStatus val);
std::string serialize(BspEventKind val);
std::string serialize(SlacStateKind val);

std::string serialize(BptParams const& val) noexcept;
std::string serialize(CurvePoint const& val) noexcept;
std::string serialize(ChargingCurve const& val) noexcept;
std::string serialize(SessionConfigParams const& val) noexcept;
std::string serialize(SetChargingCurrentParams const& val) noexcept;
std::string serialize(SetSocParams const& val) noexcept;
std::string serialize(BcbToggleParams const& val) noexcept;
std::string serialize(InjectFaultParams const& val) noexcept;
std::string serialize(ScenarioTimingOverrides const& val) noexcept;
std::string serialize(RunScenarioParams const& val) noexcept;
std::string serialize(EvInfo const& val) noexcept;
std::string serialize(IsoSessionEvent const& val) noexcept;
std::string serialize(BspEvent const& val) noexcept;
std::string serialize(SlacState const& val) noexcept;
std::string serialize(FaultReport const& val) noexcept;
std::string serialize(CommandAck const& val) noexcept;

std::ostream& operator<<(std::ostream& os, FsmState const& val);
std::ostream& operator<<(std::ostream& os, ChargeMode const& val);
std::ostream& operator<<(std::ostream& os, PaymentOption const& val);
std::ostream& operator<<(std::ostream& os, FaultType const& val);
std::ostream& operator<<(std::ostream& os, ScenarioName const& val);
std::ostream& operator<<(std::ostream& os, IsoSessionEventKind const& val);
std::ostream& operator<<(std::ostream& os, CommandAckStatus const& val);
std::ostream& operator<<(std::ostream& os, BspEventKind const& val);
std::ostream& operator<<(std::ostream& os, SlacStateKind const& val);

std::ostream& operator<<(std::ostream& os, BptParams const& val);
std::ostream& operator<<(std::ostream& os, CurvePoint const& val);
std::ostream& operator<<(std::ostream& os, ChargingCurve const& val);
std::ostream& operator<<(std::ostream& os, SessionConfigParams const& val);
std::ostream& operator<<(std::ostream& os, SetChargingCurrentParams const& val);
std::ostream& operator<<(std::ostream& os, SetSocParams const& val);
std::ostream& operator<<(std::ostream& os, BcbToggleParams const& val);
std::ostream& operator<<(std::ostream& os, InjectFaultParams const& val);
std::ostream& operator<<(std::ostream& os, ScenarioTimingOverrides const& val);
std::ostream& operator<<(std::ostream& os, RunScenarioParams const& val);
std::ostream& operator<<(std::ostream& os, EvInfo const& val);
std::ostream& operator<<(std::ostream& os, IsoSessionEvent const& val);
std::ostream& operator<<(std::ostream& os, BspEvent const& val);
std::ostream& operator<<(std::ostream& os, SlacState const& val);
std::ostream& operator<<(std::ostream& os, FaultReport const& val);
std::ostream& operator<<(std::ostream& os, CommandAck const& val);

template <class T> T deserialize(std::string const& val);
// On parse failure, write a single-line diagnostic to stderr so the failure
// is observable in module logs. EVLOG_* is unavailable in this library; the
// stderr stream is captured by the framework logging sink at module level.
template <class T> std::optional<T> try_deserialize(std::string const& val) {
    try {
        return deserialize<T>(val);
    } catch (const std::exception& e) {
        std::cerr << "ev_simulator::try_deserialize<" << typeid(T).name() << "> failed: " << e.what()
                  << " payload=" << val << std::endl;
        return std::nullopt;
    }
}
template <class T> bool adl_deserialize(std::string const& json_data, T& obj) {
    auto opt = try_deserialize<T>(json_data);
    if (opt) {
        obj = opt.value();
        return true;
    }
    return false;
}

} // namespace everest::lib::API::V1_0::types::ev_simulator
