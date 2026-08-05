// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/d20/config.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>

namespace iso15118::d20::state {

message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse
handle_request(const message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest& req, d20::Session& session,
               const d20::AcTransferLimits& limits, const d20::AcPresentPower& powers,
               const std::optional<d20::SaeDerTransferLimits>& sae_limits,
               const std::optional<DerSaeSetupConfig>& config);

} // namespace iso15118::d20::state
