// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cassert>
#include <memory>

#include <iso15118/detail/cb_exi.hpp>
#include <iso15118/message_din/variant.hpp>

struct din_MessageHeaderType;

namespace iso15118::message_din {

// In DIN SPEC 70121 the header lives at V2G_Message level and is shared by all message bodies.
// The header pointer is carried here so insert_type can populate the message's header member.
struct VariantAccess {
    // input
    exi_bitstream_t input_stream;
    const din_MessageHeaderType* header{nullptr};

    // output
    std::unique_ptr<void, iso15118::message_din::Variant::CustomDeleter>& data;
    iso15118::message_din::Type& type;
    std::string& error;

    template <typename MessageType, typename CbExiMessageType> void insert_type(const CbExiMessageType& in) {
        assert(data == nullptr);

        // Build fully before publishing: if convert() throws, the message is released here and
        // neither data nor type is touched, leaving the Variant unchanged.
        auto msg = std::make_unique<MessageType>();
        convert(in, *msg);
        convert(*header, msg->header);

        type = iso15118::message_din::TypeTrait<MessageType>::type;
        data = {msg.release(), [](void* ptr) { delete static_cast<MessageType*>(ptr); }};
    };
};

template <typename CbExiMessageType> void insert_type(VariantAccess& va, const CbExiMessageType&);

template <typename MessageType> int serialize_to_exi(const MessageType& in, exi_bitstream_t& out);

} // namespace iso15118::message_din
