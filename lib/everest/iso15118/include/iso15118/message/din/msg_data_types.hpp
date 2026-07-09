// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>

#include <cbv2g/din/din_msgDefDatatypes.h>

namespace iso15118::din::msg {

template <typename InType, typename OutType> void convert(const InType&, OutType&);

namespace data_types {

constexpr auto SESSION_ID_LENGTH = 8;
using SESSION_ID = std::array<uint8_t, SESSION_ID_LENGTH>; // hexBinary, max length 8

} // namespace data_types

} // namespace iso15118::din::msg
