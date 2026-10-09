// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>
#include <vector>

#include <iso15118/ev/ac_charge_params.hpp>
#include <iso15118/ev/config.hpp>
#include <iso15118/ev/dc_charge_params.hpp>

namespace iso15118::ev {

/**
 * @brief Report the problems in an \ref EvConfig.
 * @details Rejects, for every energy service:
 * - a negative \c response_timeout
 * - an \c evcc_id outside 1 to 255 characters
 * - an empty protocol offer
 * - \c enforce_tls without SDP on a non-TLS \c direct_security
 * - zero \c cpd_rounds
 *
 * and, for \c AC_DER_SAE only, in \c sae_profile:
 * - an inverter identity string longer than 32 bytes
 * - \c supported_modes with bits outside \c sae::SAE_MODE_BITMAP_MASK, or without
 *   ChargeFunction and DischargeFunction
 * - a nominal, minimum or maximum voltage that is not finite and positive
 * - once those pass, a maximum voltage not above the minimum, else a nominal voltage
 *   outside the window
 * - a non-finite \c nominal_voltage_offset_v
 * - a \c nominal_frequency_hz that is not finite and positive
 * - a power factor outside (0, 1]
 * - a VA, var, susceptance or excited discharge power total that is not finite and
 *   non-negative
 *
 * Consumers surface the messages and refuse the session.
 * @param[in] config The configuration to check.
 * @return One message per problem, empty if the config is usable.
 */
std::vector<std::string> validate_config(const EvConfig& config);

/**
 * @brief Report the problems in an \ref AcChargeParams limit set.
 * @details Limits must be non-negative and no min may exceed its max. The present-power
 * measurement is not checked.
 * @param[in] params The limit set to check.
 * @return One message per problem, empty if the limits are usable.
 */
std::vector<std::string> validate_ac_charge_params(const AcChargeParams& params);

/**
 * @brief Report the problems in a \ref DcChargeParams limit set.
 * @details Same contract as \ref validate_ac_charge_params, for the DC limits
 * advertised in DC_ChargeParameterDiscovery.
 * @param[in] params The limit set to check.
 * @return One message per problem, empty if the limits are usable.
 */
std::vector<std::string> validate_dc_charge_params(const DcChargeParams& params);

} // namespace iso15118::ev
