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
 * @details Nothing here is recoverable by the stack: a non-positive response timeout
 * disarms the response watchdog, and a non MAC-formatted \c evcc_id goes on the wire
 * as the EVCCID and is rejected by the SECC. Consumers surface the messages and
 * refuse the session rather than starting one that cannot succeed.
 * @param[in] config The configuration to check.
 * @return One message per problem, empty if the config is usable.
 */
std::vector<std::string> validate_config(const EvConfig& config);

/**
 * @brief Report the problems in an \ref AcChargeParams limit set.
 * @details Checks the advertised static limits only (the live present-power field is
 * a measurement, not a limit): every limit must be non-negative and each min must not
 * exceed its max, because the pair is advertised verbatim in
 * AC_ChargeParameterDiscovery.
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
