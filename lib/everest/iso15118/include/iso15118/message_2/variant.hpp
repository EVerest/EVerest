// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <iso15118/io/sdp.hpp>
#include <iso15118/io/stream_view.hpp>

#include "common_types.hpp"
#include "type.hpp"

namespace iso15118::message_2 {

class Variant {
public:
    using CustomDeleter = void (*)(void*);
    explicit Variant(const io::StreamInputView&);
    template <typename MessageType> Variant(const MessageType& in) {
        static_assert(TypeTrait<MessageType>::type != Type::None, "Unhandled type!");

        data = {new MessageType(in), [](void* ptr) { delete static_cast<MessageType*>(ptr); }};
        type = message_2::TypeTrait<MessageType>::type;
    }

    Type get_type() const;

    const std::string& get_error() const;

    const datatypes::SessionId& get_session_id() const;

    // Raw EXI payload the variant was decoded from (empty for a variant built directly from a C++
    // message). Used by the PnC signature verification, which must re-decode the request to recover
    // the cbv2g iso2 structs needed to rebuild the signed EXI fragment.
    const std::vector<uint8_t>& get_exi_payload() const {
        return exi_payload;
    }

    template <typename T> const T& get() const {
        static_assert(TypeTrait<T>::type != Type::None, "Unhandled type!");
        // data is null for the relay-only types (CertificateInstallationReq / CertificateUpdateReq): the
        // type is known but no message struct was decoded, so accessing one is a programming error
        // rather than a null dereference. get_if() returns nullptr for them.
        if (TypeTrait<T>::type != type or data == nullptr) {
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
    datatypes::SessionId session_id{};
    std::string error;
    std::vector<uint8_t> exi_payload;
};
} // namespace iso15118::message_2
