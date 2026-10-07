// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/base64.hpp>

#include <everest/tls/openssl_util.hpp>

namespace iso15118 {

std::string base64_encode(const std::vector<uint8_t>& data) {
    return openssl::base64_encode(data.data(), data.size());
}

std::vector<uint8_t> base64_decode(const std::string& in) {
    return openssl::base64_decode(in.data(), in.size());
}

} // namespace iso15118
