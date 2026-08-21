// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/der_functions.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/sae_modes.hpp>

namespace iso15118::d20::state {

namespace dt_sae = message_20::datatypes::sae;

void convert(dt_sae::CurveDataPointsList& out, const sae::CurveDataPointsList& in);
void convert(dt_sae::DERCurve& out, const sae::DERCurve& in);
void convert(dt_sae::VoltageTrip& out, const sae::VoltageTrip& in);
void convert(dt_sae::FrequencyTrip& out, const sae::FrequencyTrip& in);
void convert(dt_sae::EnterServiceCPDRes& out, const sae::EnterServiceCPDRes& in);

void convert(dt_sae::ConstantPowerFactor& out, const sae::ConstantPowerFactor& in);
void convert(dt_sae::VoltVar& out, const sae::VoltVar& in);
void convert(dt_sae::WattVar& out, const sae::WattVar& in);
void convert(dt_sae::ConstantVar& out, const sae::ConstantVar& in);
void convert(dt_sae::ReactivePowerSupportCPDRes& out, const sae::ReactivePowerSupportCPDRes& in);

void convert(dt_sae::FrequencyDroopSettings& out, const sae::FrequencyDroopSettings& in);
void convert(dt_sae::FrequencyDroop& out, const sae::FrequencyDroop& in);
void convert(dt_sae::VoltWatt& out, const sae::VoltWatt& in);
void convert(dt_sae::ConstantWatt& out, const sae::ConstantWatt& in);
void convert(dt_sae::LimitMaxDischargePower& out, const sae::LimitMaxDischargePower& in);
void convert(dt_sae::ActivePowerSupportCPDRes& out, const sae::ActivePowerSupportCPDRes& in);

void convert(dt_sae::DERControlCPDRes& out, const sae::DERControl& in);

void convert(dt_sae::EnterServiceCLRes& out, const sae::EnterServiceCPDRes& in);
void convert(dt_sae::ReactivePowerSupportCLRes& out, const sae::ReactivePowerSupportCPDRes& in);
void convert(dt_sae::ActivePowerSupportCLRes& out, const sae::ActivePowerSupportCPDRes& in);

// The mode helpers moved to <iso15118/sae_modes.hpp>; the SECC call sites keep their unqualified names.
using sae::derive_enabled_modes;
using sae::is_function_set;
using sae::SAE_ENABLED_MODE_MASK;
using sae::sae_function_bit;
using sae::sae_function_names;
using sae::SAE_MODE_BITMAP_MASK;

void gate_enable(bool& enable, bool supported, const char* function_name);
void gate_optional_curve(std::optional<dt_sae::DERCurve>& curve, bool supported, const char* function_name);

void gate_enables_by_supported_modes(dt_sae::DERControlCPDRes& out, std::uint32_t supported_modes);

// DERControlCLRes is the channel for updating parameters already sent in DERControlCPDRes, so the optional
// blocks are emitted only when those parameters actually change. Not purely additive: on the no change path
// the optional blocks and the optional enter service members are cleared from out.
void build_der_control_cl_res(dt_sae::DERControlCLRes& out, const DerSaeSetupConfig& config, bool changed_since_cpd,
                              std::uint32_t ev_supported_modes);

} // namespace iso15118::d20::state
