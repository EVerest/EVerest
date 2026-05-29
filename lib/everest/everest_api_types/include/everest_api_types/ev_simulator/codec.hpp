// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "API.hpp"
#include <optional>
#include <string>

namespace everest::lib::API::V1_0::types::ev_simulator {

std::string serialize(FsmState val) noexcept;
std::string serialize(ChargeMode val) noexcept;
std::string serialize(PaymentOption val) noexcept;
std::string serialize(FaultType val) noexcept;
std::string serialize(ScenarioName val) noexcept;
std::string serialize(IsoSessionEventKind val) noexcept;
std::string serialize(CommandAckStatus val) noexcept;

std::string serialize(StartSessionParams const& val) noexcept;
std::string serialize(SetChargingCurrentParams const& val) noexcept;
std::string serialize(SetSocParams const& val) noexcept;
std::string serialize(BcbToggleParams const& val) noexcept;
std::string serialize(InjectFaultParams const& val) noexcept;
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

std::ostream& operator<<(std::ostream& os, StartSessionParams const& val);
std::ostream& operator<<(std::ostream& os, SetChargingCurrentParams const& val);
std::ostream& operator<<(std::ostream& os, SetSocParams const& val);
std::ostream& operator<<(std::ostream& os, BcbToggleParams const& val);
std::ostream& operator<<(std::ostream& os, InjectFaultParams const& val);
std::ostream& operator<<(std::ostream& os, RunScenarioParams const& val);
std::ostream& operator<<(std::ostream& os, EvInfo const& val);
std::ostream& operator<<(std::ostream& os, IsoSessionEvent const& val);
std::ostream& operator<<(std::ostream& os, BspEvent const& val);
std::ostream& operator<<(std::ostream& os, SlacState const& val);
std::ostream& operator<<(std::ostream& os, FaultReport const& val);
std::ostream& operator<<(std::ostream& os, CommandAck const& val);

template <class T> T deserialize(std::string const& val);
template <class T> std::optional<T> try_deserialize(std::string const& val) {
    try {
        return deserialize<T>(val);
    } catch (...) {
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
