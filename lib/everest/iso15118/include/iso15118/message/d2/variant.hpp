// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>

#include "type.hpp"

namespace iso15118::msg::d2 {

class Variant {
public:
    Variant();
    explicit Variant(const io::StreamInputView&);
    template <typename MessageType> Variant(MessageType&& message, Type type_);
    ~Variant();

    template <typename T> T get();
    template <typename T> T* get_if();

    [[nodiscard]] Type get_type() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl;
    Type type{Type::None};
};

} // namespace iso15118::msg::d2
