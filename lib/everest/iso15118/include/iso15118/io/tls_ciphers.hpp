// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

namespace iso15118::io {

// Note: openssl does not provide support for ECDH-ECDSA-AES128-SHA256 anymore
constexpr auto TLS1_2_CIPHERSUITES = "ECDHE-ECDSA-AES128-SHA256";
constexpr auto TLS1_3_CIPHERSUITES = "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";

} // namespace iso15118::io
