// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <iso15118/io/stream_view.hpp>

#include "type.hpp"

namespace iso15118::message_2 {

// The EV counterpart of Variant: decodes only what the EV receives, so no *Req type.
class ResponseVariant {
public:
    using CustomDeleter = void (*)(void*);
    explicit ResponseVariant(const io::StreamInputView&);
    template <typename MessageType> ResponseVariant(const MessageType& in) {
        static_assert(TypeTrait<MessageType>::type != Type::None, "Unhandled type!");

        data = {new MessageType(in), [](void* ptr) { delete static_cast<MessageType*>(ptr); }};
        type = message_2::TypeTrait<MessageType>::type;
    }

    Type get_type() const;

    const std::string& get_error() const;

    // Empty for a variant built directly from a C++ message. The CertificateInstallationRes signature
    // is verified over these bytes.
    const std::vector<uint8_t>& get_exi_payload() const {
        return exi_payload;
    }

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
    std::vector<uint8_t> exi_payload;
};

} // namespace iso15118::message_2
