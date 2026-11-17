// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <stdexcept>
#include <utility>

// The value of `opt`. Throws, and so fails the running test, when `opt` is empty.
template <typename T> const T& value_of(const std::optional<T>& opt) {
    if (!opt.has_value()) {
        throw std::logic_error("expected a value, got an empty optional");
    }
    return *opt;
}

template <typename T> T value_of(std::optional<T>&& opt) {
    if (!opt.has_value()) {
        throw std::logic_error("expected a value, got an empty optional");
    }
    return std::move(*opt);
}
