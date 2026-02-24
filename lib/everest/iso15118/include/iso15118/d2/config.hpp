// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>

namespace iso15118::d2 {

struct EvseSetupConfig {
    std::string evse_id;
};

// This should only have EVSE information
struct SessionConfig {
    explicit SessionConfig(EvseSetupConfig config) : evse_id(std::move(config.evse_id)) {};

    std::string evse_id;
};

} // namespace iso15118::d2
