// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/din/msg_data_types.hpp>

namespace iso15118::din::msg {

namespace data_types {} // namespace data_types

struct ContractAuthenticationRequest {
    Header header;
    std::optional<data_types::IDREF> id;
    std::optional<data_types::GenChallenge> gen_challenge;
};

struct ContractAuthenticationResponse {
    Header header;
    data_types::ResponseCode response_code;
    data_types::EvseProcessing evse_processing;
};

} // namespace iso15118::din::msg