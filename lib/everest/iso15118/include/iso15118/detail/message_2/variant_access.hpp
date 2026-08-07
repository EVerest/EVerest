// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cassert>
#include <stdexcept>
#include <string>

#include <cbv2g/common/exi_bitstream.h>

#include <iso15118/detail/cb_exi.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/variant.hpp>

namespace iso15118::message_2 {

struct VariantAccess {
    // input (decoded once at the variant level, since ISO 15118-2 uses a single grammar)
    const iso15118::message_2::Header& header;

    // output
    void*& data;
    iso15118::message_2::Type& type;
    iso15118::message_2::Variant::CustomDeleter& custom_deleter;
    std::string& error;

    template <typename MessageType, typename CbExiMessageType> void insert_type(const CbExiMessageType& in) {
        assert(data == nullptr);

        auto* const msg = new MessageType;
        data = msg;
        type = iso15118::message_2::TypeTrait<MessageType>::type;
        custom_deleter = [](void* ptr) { delete static_cast<MessageType*>(ptr); };

        convert(in, *msg);
        msg->header = header;
    };
};

template <typename CbExiMessageType> void insert_type(VariantAccess& va, const CbExiMessageType&);

template <typename MessageType> int serialize_to_exi(const MessageType& in, exi_bitstream_t& out);

} // namespace iso15118::message_2
