// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/limits.hpp>

namespace iso15118::d20 {

/// \brief Returns a description of the first conformance violation in the SAE DER setup, or nullopt.
///
/// SessionConfig uses this to decide whether AC_DER_SAE may be offered. Exposed for testing.
std::optional<std::string> validate_sae_der_setup(const DerSaeSetupConfig& setup_config,
                                                  const SaeDerTransferLimits& sae_limits);

} // namespace iso15118::d20
