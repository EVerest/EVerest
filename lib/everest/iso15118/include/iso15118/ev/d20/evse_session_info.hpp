// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/common_types.hpp>

namespace iso15118::ev::d20 {

// Holds information reported by the EVSE that could be different between sessions
struct EVSESessionInfo {
    std::vector<message_20::datatypes::Authorization> auth_services{};
    bool certificate_installation_service{false};
    std::optional<std::vector<message_20::datatypes::Name>> supported_providers{std::nullopt};
    message_20::datatypes::GenChallenge gen_challenge{std::array<uint8_t, 16>{}}; // 16 bytes
};

} // namespace iso15118::ev::d20
