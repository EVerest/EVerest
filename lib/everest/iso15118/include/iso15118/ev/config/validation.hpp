// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>
#include <vector>

#include <iso15118/ev/config/config.hpp>
#include <iso15118/ev/params/ac.hpp>
#include <iso15118/ev/params/dc.hpp>

namespace iso15118::ev::config {

/**
 * @brief Report the problems in an \ref EvConfig.
 * @details Rejects, for every energy service:
 * - a negative \c response_timeout
 * - an \c evcc_id outside 1 to 255 characters
 * - an empty SupportedAppProtocolReq offer: no protocol enabled, or none the energy service and
 *   transport allow
 * - SDP disabled without a \c direct_secc_endpoint
 * - \c tls.mode Require without SDP on a non-TLS \c direct_security
 * - \c tls.version Auto with no TLS version to offer, see \ref auto_tls_problem
 * - zero \c cpd_rounds
 * - welding detection voltage that is not finite and positive, fewer than one cycle, or a non-positive timeout
 *
 * and, for \c AC_DER_SAE only, the \c sae_profile problems \ref validate_sae_profile reports.
 *
 * Consumers surface the messages and refuse the session.
 * @param[in] config The configuration to check.
 * @return One message per problem, empty if the config is usable.
 */
std::vector<std::string> validate_config(const EvConfig& config);

/**
 * @brief Report the problems in an AC_DER_SAE inverter profile.
 * @details Rejects:
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
 * \ref validate_config runs it for an AC_DER_SAE session; a consumer can run it once on a profile
 * it loads.
 * @param[in] profile The profile to check.
 * @return One message per problem, empty if the profile is usable.
 */
std::vector<std::string> validate_sae_profile(const d20::der::SaeInverterProfile& profile);

/**
 * @brief Report each protocol a pinned TLS version runs outside the version its standard requires.
 * @details ISO 15118-2 over a pinned TLS 1.3 and ISO 15118-20 over a pinned TLS 1.2. Auto narrows
 * the offer to the negotiated version instead, and DIN SPEC 70121 is never offered over TLS.
 * Consumers warn and still run the session.
 * @param[in] config The configuration to check.
 * @return One message per deviation, empty if there is none.
 */
std::vector<std::string> tls_deviations(const EvConfig& config);

/**
 * @brief Report why tls_version auto has no TLS version left to offer.
 * @details 1.2 needs an ISO 15118-2 entry in the offer, 1.3 an ISO 15118-20 entry and a usable
 * vehicle chain. \ref validate_config passes the chain problem it can see without the files, empty
 * paths; a Controller passes the one it finds at connect.
 * @param[in] config The configuration to check.
 * @param[in] chain_problem Why the vehicle chain cannot back TLS 1.3, empty when it can.
 * @return The problem, empty when a version is left or TLS is off or pinned.
 */
std::string auto_tls_problem(const EvConfig& config, const std::string& chain_problem);

/**
 * @brief Report the problems in an \ref params::AcChargeParams limit set.
 * @details Limits must be non-negative and no min may exceed its max. The present-power
 * measurement is not checked.
 * @param[in] params The limit set to check.
 * @return One message per problem, empty if the limits are usable.
 */
std::vector<std::string> validate_ac_charge_params(const params::AcChargeParams& params);

/**
 * @brief Report the problems in a \ref params::DcChargeParams limit set.
 * @details Same contract as \ref validate_ac_charge_params, for the DC limits
 * advertised in DC_ChargeParameterDiscovery.
 * @param[in] params The limit set to check.
 * @return One message per problem, empty if the limits are usable.
 */
std::vector<std::string> validate_dc_charge_params(const params::DcChargeParams& params);

} // namespace iso15118::ev::config
