// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d2/config.hpp>
#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/session/config.hpp>

namespace iso15118 {

// Copies the (d20) DC transfer limits -- what energy management currently grants -- onto the ISO 15118-2
// charge-loop values (CurrentDemandRes). Used both when the session config is built and when a
// DcTransferLimits control event arrives mid-session; see the DIN counterpart. Safety: no defaults --
// negative (invalid) values clamp to 0 and unreported ones stay 0, so the EV is never told it may draw
// energy nobody reported.
void apply_dc_limits(d2::SessionConfig& out, const d20::DcTransferLimits& dc);

// Copies the power-supply hardware capabilities onto the values ChargeParameterDiscoveryRes advertises
// (the EVSEMaximum*/EVSEMinimum* offer and the SAScheduleList PMax): the maximum the EVSE could ever
// deliver, kept apart from the live limits above (EvseV2G parity). Safety: no defaults -- negative
// (invalid) values clamp to 0 and unreported ones stay 0, so only real data advertises a positive offer.
void apply_dc_capabilities(d2::SessionConfig& out, const d20::DcTransferLimits& dc);

// Copies the module-reported physical EVSE parameters (set_charging_parameters) into the ISO 15118-2
// config. EVSENominalVoltage and EVSEPeakCurrentRipple are mandatory elements and keep their current
// values when the module reports none; the other two are optional and stay absent. The AC max current is
// derived from the nominal voltage by the caller, not here.
void apply_physical_values(d2::SessionConfig& out, const d20::PhysicalValues& values);

// Builds the SECC-side ISO 15118-2 config from the protocol-neutral session config: EVSEID (falling back
// to the default when shorter than the schema minimum), the advertised energy transfer modes, DC/AC limits,
// physical values, PnC settings and a pending no-energy pause.
d2::SessionConfig make_d2_config(const session::SessionConfig& config, bool tls_active);

} // namespace iso15118
