// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <vector>

#include <iso15118/ev/config/config.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/session/sap_offer.hpp>

namespace iso15118::ev {

std::chrono::milliseconds stop_grace(const config::EvConfig& config);

// The ISO 15118-20 session options the Controller builds from its EvConfig.
d20::SessionOptions make_session_options(const config::EvConfig& config, std::vector<session::OfferedProtocol> offer);

} // namespace iso15118::ev
