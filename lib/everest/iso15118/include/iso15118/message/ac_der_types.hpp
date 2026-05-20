// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

namespace iso15118::message_20 {

namespace datatypes {

enum class PowerFactorExcitation : std::uint8_t {
    OverExcited,
    UnderExcited
};

enum class IEEE1547NormalCategory : std::uint8_t {
    CategoryA,
    CategoryB,
};

enum class IEEE1547AbnormalCategory : std::uint8_t {
    CategoryI,
    CategoryII,
    CategoryIII,
};

} // namespace datatypes

} // namespace iso15118::message_20
