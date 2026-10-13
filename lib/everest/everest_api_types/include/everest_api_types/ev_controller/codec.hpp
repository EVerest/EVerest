// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "API.hpp"
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::ev_controller {

std::string serialize(FsmState val);
std::string serialize(ChargeMode val);
std::string serialize(PaymentOption val);
std::string serialize(DerFlavor val);
std::string serialize(DerControlSource val);
std::string serialize(FaultType val);
std::string serialize(IsoSessionEventKind val);
std::string serialize(CommandAckStatus val);
std::string serialize(BspEventKind val);
std::string serialize(SlacStateKind val);

std::string serialize(BptParams const& val) noexcept;
std::string serialize(DerNegotiatedFunctions const& val) noexcept;
std::string serialize(DerControl const& val) noexcept;
std::string serialize(CurvePoint const& val) noexcept;
std::string serialize(ChargingCurve const& val) noexcept;
std::string serialize(SessionConfigParams const& val) noexcept;
std::string serialize(SetChargingCurrentParams const& val) noexcept;
std::string serialize(SetSocParams const& val) noexcept;
std::string serialize(BcbToggleParams const& val) noexcept;
std::string serialize(SetPresentValuesParams const& val) noexcept;
std::string serialize(StopSessionParams const& val) noexcept;
std::string serialize(EvInfo const& val) noexcept;
std::string serialize(IsoSessionEvent const& val) noexcept;
std::string serialize(BspEvent const& val) noexcept;
std::string serialize(SlacState const& val) noexcept;
std::string serialize(FaultReport const& val) noexcept;
std::string serialize(CommandAck const& val) noexcept;

std::ostream& operator<<(std::ostream& os, FsmState const& val);
std::ostream& operator<<(std::ostream& os, ChargeMode const& val);
std::ostream& operator<<(std::ostream& os, PaymentOption const& val);
std::ostream& operator<<(std::ostream& os, DerFlavor const& val);
std::ostream& operator<<(std::ostream& os, DerControlSource const& val);
std::ostream& operator<<(std::ostream& os, FaultType const& val);
std::ostream& operator<<(std::ostream& os, IsoSessionEventKind const& val);
std::ostream& operator<<(std::ostream& os, CommandAckStatus const& val);
std::ostream& operator<<(std::ostream& os, BspEventKind const& val);
std::ostream& operator<<(std::ostream& os, SlacStateKind const& val);

std::ostream& operator<<(std::ostream& os, BptParams const& val);
std::ostream& operator<<(std::ostream& os, DerNegotiatedFunctions const& val);
std::ostream& operator<<(std::ostream& os, DerControl const& val);
std::ostream& operator<<(std::ostream& os, CurvePoint const& val);
std::ostream& operator<<(std::ostream& os, ChargingCurve const& val);
std::ostream& operator<<(std::ostream& os, SessionConfigParams const& val);
std::ostream& operator<<(std::ostream& os, SetChargingCurrentParams const& val);
std::ostream& operator<<(std::ostream& os, SetSocParams const& val);
std::ostream& operator<<(std::ostream& os, BcbToggleParams const& val);
std::ostream& operator<<(std::ostream& os, SetPresentValuesParams const& val);
std::ostream& operator<<(std::ostream& os, StopSessionParams const& val);
std::ostream& operator<<(std::ostream& os, EvInfo const& val);
std::ostream& operator<<(std::ostream& os, IsoSessionEvent const& val);
std::ostream& operator<<(std::ostream& os, BspEvent const& val);
std::ostream& operator<<(std::ostream& os, SlacState const& val);
std::ostream& operator<<(std::ostream& os, FaultReport const& val);
std::ostream& operator<<(std::ostream& os, CommandAck const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::ev_controller
