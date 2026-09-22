// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <vector>

#include <iso15118/ev/config.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/sap_offer.hpp>

namespace iso15118::ev {

// The ISO 15118-20 session options the Controller builds from its EvConfig.
d20::SessionOptions make_session_options(const EvConfig& config, std::vector<OfferedProtocol> offer);

} // namespace iso15118::ev
