// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cassert>
#include <memory>

#include <iso15118/message/variant.hpp>

#include "cb_exi.hpp"

namespace iso15118::message_20 {

struct VariantAccess {
    // input
    exi_bitstream_t input_stream;

    // output
    std::unique_ptr<void, RequestVariant::CustomDeleter>& data;
    iso15118::message_20::Type& type;
    std::string& error;
    bool& undecodable;
    bool& unhandled_message;

    void mark_unhandled() {
        unhandled_message = true;
        error = "chosen message type unhandled";
    }

    template <typename MessageType, typename CbExiMessageType> void insert_type(const CbExiMessageType& in) {
        assert(data.get() == nullptr);
        type = iso15118::message_20::TypeTrait<MessageType>::type;

        data = std::unique_ptr<void, RequestVariant::CustomDeleter>(
            new MessageType, [](void* ptr) { delete static_cast<MessageType*>(ptr); });
        convert(in, *static_cast<MessageType*>(data.get()));
    };
};

template <typename CbExiMessageType> void insert_type(VariantAccess& va, const CbExiMessageType&);

} // namespace iso15118::message_20
