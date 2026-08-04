// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>

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

} // namespace iso15118::d20::state
