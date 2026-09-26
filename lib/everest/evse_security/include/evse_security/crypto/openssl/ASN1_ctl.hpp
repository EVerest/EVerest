// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <evse_security/utils/load_ctl.hpp>

#include <cstdint>
#include <vector>

namespace ctl {

// Throws evse_security::CertificateLoadException / std::runtime_error on failure.
std::vector<std::uint8_t> encode_der(const TrustList& ctl);

// Throws on parse failure. Caller should run ctl::validate() afterwards.
TrustList decode_der(const std::uint8_t* der, std::size_t len);
TrustList decode_der(const std::vector<std::uint8_t>& der);

} // namespace ctl