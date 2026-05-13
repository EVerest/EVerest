// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/common_types.hpp>

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::ev::d20 {

// Holds information reported by the EVSE that could be different between sessions
struct EVSESessionInfo {
    everest::lib::util::fixed_vector<message_20::datatypes::Authorization, 2> auth_services{};
    bool certificate_installation_service{false};
    std::optional<everest::lib::util::fixed_vector<message_20::datatypes::Name, 128>> supported_providers{std::nullopt};
    message_20::datatypes::GenChallenge gen_challenge{std::array<uint8_t, 16>{}}; // 16 bytes
};

} // namespace iso15118::ev::d20
