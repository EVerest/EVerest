// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

#include "common_types.hpp"

namespace iso15118::message_din {

struct ContractAuthenticationRequest {
    Header header;
    std::optional<std::string> id;
    std::optional<std::string> gen_challenge;
};

struct ContractAuthenticationResponse {
    Header header;
    datatypes::ResponseCode response_code;
    datatypes::EvseProcessing evse_processing{datatypes::EvseProcessing::Ongoing};
};

} // namespace iso15118::message_din
