// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

#include "common_types.hpp"

namespace iso15118::message_2 {

struct AuthorizationRequest {
    Header header;
    std::optional<std::string> id;
    std::optional<datatypes::GenChallenge> gen_challenge;
};

struct AuthorizationResponse {
    Header header;
    datatypes::ResponseCode response_code;
    datatypes::EVSEProcessing evse_processing;
};

} // namespace iso15118::message_2
