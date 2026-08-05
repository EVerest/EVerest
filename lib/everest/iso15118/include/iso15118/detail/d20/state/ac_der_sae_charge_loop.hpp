// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/d20/config.hpp>
#include <iso15118/d20/dynamic_mode_parameters.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/d20/state/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>

namespace iso15118::d20::state {

message_20::DER_SAE_AC_ChargeLoopResponse
handle_request(const message_20::DER_SAE_AC_ChargeLoopRequest& req, const d20::Session& session, bool stop, bool pause,
               const AcTargetPower& target_powers, const d20::AcPresentPower& present_powers,
               const UpdateDynamicModeParameters& dynamic_parameters, const d20::AcTransferLimits& ac_limits,
               const std::optional<d20::SaeDerTransferLimits>& sae_limits,
               const std::optional<d20::DerSaeSetupConfig>& der_config, bool changed_since_cpd,
               SaeChargeLoopLogState& log_state);

} // namespace iso15118::d20::state
