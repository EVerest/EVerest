// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>

namespace test_contract {

struct Contract {
    std::string chain_pem;
    std::string key_pem;
};

// A self-signed prime256v1 contract leaf with the given CommonName, and its key, generated on each call.
Contract generate(const std::string& common_name);

} // namespace test_contract
