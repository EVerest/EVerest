// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>
#include <stdexcept>
#include <string>

#include <iso15118/io/stream_view.hpp>

#include "type.hpp"

namespace iso15118::message_din {

// The EV counterpart of Variant: decodes only what the EV receives, so no *Req type.
class ResponseVariant {
public:
    using CustomDeleter = void (*)(void*);
    explicit ResponseVariant(const io::StreamInputView&);
    template <typename MessageType> ResponseVariant(const MessageType& in) {
        static_assert(TypeTrait<MessageType>::type != Type::None, "Unhandled type!");

        data = {new MessageType(in), [](void* ptr) { delete static_cast<MessageType*>(ptr); }};
        type = message_din::TypeTrait<MessageType>::type;
    }

    Type get_type() const;

    const std::string& get_error() const;

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
    std::unique_ptr<void, CustomDeleter> data{nullptr, nullptr};
    Type type{Type::None};
    std::string error;
};

} // namespace iso15118::message_din
