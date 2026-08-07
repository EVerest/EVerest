// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/din/config.hpp>
#include <iso15118/session/config.hpp>

namespace iso15118 {

// Copies the (d20) DC transfer limits -- what energy management currently grants -- onto the DIN
// charge-loop values (CurrentDemandRes). Used both when the session config is built and when a
// DcTransferLimits control event arrives mid-session (EvseManager keeps pushing these from energy
// management for the whole session). Safety: no defaults -- negative (invalid) values clamp to 0 and
// unreported ones stay 0, so the EV is never told it may draw energy nobody reported.
void apply_dc_limits(din::SessionConfig& cfg, const d20::DcTransferLimits& dc);

// Copies the power-supply hardware capabilities onto the values ChargeParameterDiscoveryRes advertises
// (the EVSEMaximum*/EVSEMinimum* offer and the SAScheduleList PMax): the maximum the EVSE could ever
// deliver, kept apart from the live limits above (EvseV2G parity). Safety: no defaults -- negative
// (invalid) values clamp to 0 and unreported ones stay 0, so only real data advertises a positive offer.
void apply_dc_capabilities(din::SessionConfig& cfg, const d20::DcTransferLimits& dc);

// Copies the module-reported physical EVSE parameters (set_charging_parameters) into the DIN config.
// EVSEPeakCurrentRipple is mandatory in DC_EVSEChargeParameter, so it keeps whatever value the config
// already carries when the module reports none; the other two are optional elements and stay absent.
void apply_physical_values(din::SessionConfig& cfg, const d20::PhysicalValues& values);

// Builds the SECC-side DIN SPEC 70121 config from the protocol-neutral session config: EVSEID as DIN
// hexBinary, the single offered energy transfer mode, DC limits, physical values and a pending no-energy
// pause.
din::SessionConfig make_din_config(const session::SessionConfig& config);

} // namespace iso15118
