// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include <iso15118/io/stream_view.hpp>

#include "common_types.hpp"
#include "type.hpp"

namespace iso15118::message_din {

class Variant {
public:
    using CustomDeleter = void (*)(void*);
    Variant(const io::StreamInputView&);
    template <typename MessageType> Variant(const MessageType& in) {
        static_assert(TypeTrait<MessageType>::type != Type::None, "Unhandled type!");

        data = {new MessageType(in), [](void* ptr) { delete static_cast<MessageType*>(ptr); }};
        type = message_din::TypeTrait<MessageType>::type;
        session_id = in.header.session_id;
    }

    Type get_type() const;

    const std::string& get_error() const;

    // The SessionID from the received V2G message header (all zeros if decoding failed). Used by
    // reject_unknown_session to validate a request before any of its content takes effect.
    const datatypes::SessionId& get_session_id() const;

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
    // Owning handle: binds the message pointer and its deleter together, so the two can never
    // fall out of sync and no hand-written destructor is needed. Being move-only, it also deletes
    // the implicit copy operations, which previously would have double-freed the message.
    std::unique_ptr<void, CustomDeleter> data{nullptr, nullptr};
    Type type{Type::None};
    std::string error;
    datatypes::SessionId session_id{};
};
} // namespace iso15118::message_din
