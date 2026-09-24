// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
// Pure builders of the EV's AC_DER_SAE CPD and dynamic CL request modes from SaeInverterProfile and AcChargeParams.
#pragma once

#include <cstdint>

#include <iso15118/ev/ac_charge_params.hpp>
#include <iso15118/ev/d20/secc_clock.hpp>
#include <iso15118/ev/sae_inverter_profile.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>

namespace iso15118::ev {

// Every power, var, VA and siemens total is split across the lines of the connector via
// emit_ac_limit. Power factors are ratios and go through emit_ac_ratio.

/**
 * Build the AC_DER_SAE CPD request transfer mode (AMD1 Table M.5).
 *
 * SupportedModes is \p profile.supported_modes masked with sae::SAE_MODE_BITMAP_MASK.
 * J3072CertificationDate is \p profile.j3072_certification_date in microseconds (Table M.5), saturated,
 * and moved from the Unix epoch into SECC time with \p secc_clock.
 *
 * \param enabled_modes EnabledModes, the Table M.6 bitmap.
 * \param update_time   EVUpdateTime, microseconds of SECC time (Table M.5).
 */
message_20::datatypes::sae::DER_SAE_AC_CPDReqEnergyTransferMode
make_sae_cpd_transfer_mode(const SaeInverterProfile& profile, const AcChargeParams& params,
                           message_20::datatypes::AcConnector connector, message_20::datatypes::Processing processing,
                           std::uint32_t enabled_modes, std::uint64_t update_time, const d20::SeccClock& secc_clock);

/**
 * Build the AC_DER_SAE dynamic charge-loop control mode (AMD1 Table M.8).
 *
 * present_active_power is a measurement and goes through emit_ac_present.
 *
 * \param der_alarm_status DERAlarmStatus, the Table M.9 bitmap.
 * \param enabled_modes    EnabledModes, the Table M.6 bitmap.
 * \param permit_service   the SECC's PermitService authorization for the EV to be in service as a
 *                         DER. true emits \p profile.operational_state and \p profile.connection_status,
 *                         false forces Off/Disconnected.
 * \param update_time      EVUpdateTime, microseconds of SECC time (Table M.8).
 */
message_20::datatypes::sae::DER_Dynamic_AC_CLReqControlMode
make_sae_cl_control_mode(const SaeInverterProfile& profile, const AcChargeParams& params,
                         message_20::datatypes::AcConnector connector, float present_voltage, float present_frequency,
                         std::uint32_t der_alarm_status, std::uint32_t enabled_modes, bool permit_service,
                         std::uint64_t update_time);

} // namespace iso15118::ev
