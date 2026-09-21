// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace iso15118 {

std::string base64_encode(const std::vector<uint8_t>& data);

// Empty on an invalid character; whitespace and padding are skipped.
std::vector<uint8_t> base64_decode(const std::string& in);

} // namespace iso15118
