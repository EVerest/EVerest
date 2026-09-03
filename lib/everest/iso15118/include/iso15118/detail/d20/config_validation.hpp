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
                                                  const SaeDerTransferLimits& sae_limits,
                                                  const AcTransferLimits& ac_limits);

/// \brief Returns a description of the first SAE nominal power that is not covered by its maximum, or nullopt.
///
/// Charge nominals are compared against the AC charge power maxima, discharge nominals against the SAE maximum
/// discharge power by magnitude. A nominal whose maximum is absent is a violation. Runs at offer time through
/// validate_sae_der_setup and again at charge parameter discovery, since the AC limits can change in between.
std::optional<std::string> validate_sae_nominals_within_maxima(const SaeDerTransferLimits& sae_limits,
                                                               const AcTransferLimits& ac_limits);

/// \brief Installs a grid code delivered to a running session, or keeps the previous one.
///
/// The setup is validated against the session's SAE and AC limits first. A violation, or missing SAE limits,
/// leaves the installed grid code untouched and is warned about. Returns whether the setup was installed.
bool install_der_sae_setup_config(SessionConfig& session_config, const DerSaeSetupConfig& setup_config);

} // namespace iso15118::d20
