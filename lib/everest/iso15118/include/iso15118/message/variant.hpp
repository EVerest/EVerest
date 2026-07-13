// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

// FIXME (aw): we only need the payload types from sdp.hpp, this could be shared in a separate header file
#include <iso15118/io/sdp.hpp>
#include <iso15118/io/stream_view.hpp>

#include "type.hpp"

namespace iso15118::message_20 {

class Variant {
public:
    using CustomDeleter = std::function<void(void*)>;
    Variant(io::v2gtp::PayloadType, const io::StreamInputView&);
    template <typename MessageType>
    explicit Variant(const MessageType& in_) :
        data(new MessageType, [](void* ptr) { delete static_cast<MessageType*>(ptr); }),
        type(message_20::TypeTrait<MessageType>::type) {
        static_assert(TypeTrait<MessageType>::type != Type::None, "Unhandled type!");
        *static_cast<MessageType*>(data.get()) = in_;
    }
    ~Variant() = default;

    [[nodiscard]] Type get_type() const;
    [[nodiscard]] const std::string& get_error() const;

    template <typename T> const T& get() const {
        static_assert(TypeTrait<T>::type != Type::None, "Unhandled type!");
        if (TypeTrait<T>::type != type) {
            throw std::runtime_error("Illegal message type access");
        }

        return *static_cast<T*>(data.get());
    }

    template <typename T> T const* get_if() const {
        static_assert(TypeTrait<T>::type != Type::None, "Unhandled type!");
        if (TypeTrait<T>::type != type) {
            return nullptr;
        }

        return static_cast<T*>(data.get());
    }

private:
    std::unique_ptr<void, CustomDeleter> data;
    Type type{Type::None};
    std::string error;
};
} // namespace iso15118::message_20
