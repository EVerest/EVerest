// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include <iso15118/io/stream_view.hpp>

#include "type.hpp"

namespace iso15118::message_din {

class Variant {
public:
    using CustomDeleter = void (*)(void*);
    Variant(const io::StreamInputView&);
    template <typename MessageType> Variant(const MessageType& in) {
        static_assert(TypeTrait<MessageType>::type != Type::None, "Unhandled type!");

        data = new MessageType;
        *static_cast<MessageType*>(data) = in;
        custom_deleter = [](void* ptr) { delete static_cast<MessageType*>(ptr); };
        type = message_din::TypeTrait<MessageType>::type;
    }
    ~Variant();

    Type get_type() const;

    const std::string& get_error() const;

    template <typename T> const T& get() const {
        static_assert(TypeTrait<T>::type != Type::None, "Unhandled type!");
        if (TypeTrait<T>::type != type) {
            throw std::runtime_error("Illegal message type access");
        }

        return *static_cast<T*>(data);
    }

    template <typename T> T const* get_if() const {
        static_assert(TypeTrait<T>::type != Type::None, "Unhandled type!");
        if (TypeTrait<T>::type != type) {
            return nullptr;
        }

        return static_cast<T*>(data);
    }

private:
    CustomDeleter custom_deleter{nullptr};
    void* data{nullptr};
    Type type{Type::None};
    std::string error;
};
} // namespace iso15118::message_din
