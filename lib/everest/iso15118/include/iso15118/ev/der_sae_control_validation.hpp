// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>
#include <vector>

#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>

namespace iso15118::ev {

using DerControlProblems = std::vector<std::string>;

// Structural checks over the enabled curves and setpoints of the SECC's DER control:
// - every trip, VoltVar, WattVar and VoltWatt curve: axis units, at least 2 points per
//   list, non-decreasing x
// - ConstantVar and ConstantWatt: unit
// - ConstantPowerFactor: power factor finite and in (0, 1]
// - LimitMaxDischargePower: percentage at most 100
// EnterService and FrequencyDroop are not checked. Returns one log line per problem,
// empty if clean. Never throws or logs.
DerControlProblems validate_der_control(const message_20::datatypes::sae::DERControlCPDRes& control);
DerControlProblems validate_der_control(const message_20::datatypes::sae::DERControlCLRes& control);

} // namespace iso15118::ev
