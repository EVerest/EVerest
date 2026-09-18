// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <cbv2g/common/exi_bitstream.h>
#include <cbv2g/din/din_msgDefDecoder.h>

#include <iso15118/detail/message_din/decode_response.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message_din/type.hpp>

using namespace iso15118;

template <typename Message> std::vector<uint8_t> serialize_helper(const Message& message) {
    uint8_t serialization_buffer[1024];
    io::StreamOutputView out({serialization_buffer, sizeof(serialization_buffer)});

    const auto size = message_din::serialize(message, out);

    return std::vector<uint8_t>(serialization_buffer, serialization_buffer + size);
}

inline din_exiDocument decode_helper(std::vector<uint8_t> bytes) {
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, bytes.data(), bytes.size(), 0, nullptr);

    din_exiDocument doc;
    REQUIRE(decode_din_exiDocument(&stream, &doc) == 0);
    return doc;
}

// The SECC never decodes a response, so the tests assemble it from the cbv2g body the way an EV would.
template <typename Response, typename CbResponse>
Response to_response(const din_exiDocument& doc, const CbResponse& cb_response) {
    Response response;
    message_din::convert(doc.V2G_Message.Header, response.header);
    message_din::convert(cb_response, response);
    return response;
}
