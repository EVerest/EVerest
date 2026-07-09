// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cassert>

#include <iso15118/message/din/variant.hpp>
#include <iso15118/message/variant.hpp>

#include "cb_exi.hpp"

namespace iso15118::message_20 {

struct VariantAccess {
    // input
    exi_bitstream_t input_stream;

    // output
    void*& data;
    iso15118::message_20::Type& type;
    iso15118::message_20::Variant::CustomDeleter& custom_deleter;
    std::string& error;

    template <typename MessageType, typename CbExiMessageType> void insert_type(const CbExiMessageType& in) {
        assert(data == nullptr);

        data = new MessageType;
        type = iso15118::message_20::TypeTrait<MessageType>::type;
        custom_deleter = [](void* ptr) { delete static_cast<MessageType*>(ptr); };

        convert(in, *static_cast<MessageType*>(data));
    };
};

template <typename CbExiMessageType> void insert_type(VariantAccess& va, const CbExiMessageType&);

} // namespace iso15118::message_20

#include <cbv2g/din/din_msgDefDatatypes.h>

namespace iso15118::din::msg {

struct VariantAccess {
    // input
    exi_bitstream_t input_stream;

    // output
    void*& data;
    iso15118::din::msg::Type& type;
    iso15118::din::msg::Variant::CustomDeleter& custom_deleter;
    std::string& error;

    template <typename MessageType, typename CbExiMessageType>
    void insert_type(const CbExiMessageType& in, const din_MessageHeaderType& header) {
        assert(data == nullptr);

        data = new MessageType;
        type = iso15118::din::msg::TypeTrait<MessageType>::type;
        custom_deleter = [](void* ptr) { delete static_cast<MessageType*>(ptr); };

        auto msg = static_cast<MessageType*>(data);
        convert(header, msg->header);
        convert(in, *msg);
    };
};

template <typename CbExiMessageType>
void insert_type(VariantAccess& va, const CbExiMessageType&, const din_MessageHeaderType& header);

} // namespace iso15118::din::msg
