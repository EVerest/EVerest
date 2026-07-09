// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/message/common_types.hpp>

namespace module::charger {

inline std::optional<float>
convert_from_optional(const std::optional<iso15118::message_20::datatypes::RationalNumber>& in) {
    return (in.has_value()) ? std::make_optional(iso15118::message_20::datatypes::from_RationalNumber(in.value()))
                            : std::nullopt;
}

inline std::optional<iso15118::message_20::datatypes::RationalNumber>
convert_from_optional(const std::optional<float>& in) {
    return (in.has_value()) ? std::make_optional(iso15118::message_20::datatypes::from_float(in.value()))
                            : std::nullopt;
}

inline std::optional<float> convert_from_optional(const std::optional<uint32_t>& in) {
    return (in.has_value()) ? std::make_optional(static_cast<float>(in.value())) : std::nullopt;
}

} // namespace module::charger
