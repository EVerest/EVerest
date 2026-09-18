// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <cbv2g/common/exi_bitstream.h>
#include <cbv2g/iso_2/iso2_msgDefDecoder.h>

#include <iso15118/detail/message_2/decode_response.hpp>
#include <iso15118/io/stream_view.hpp>

using namespace iso15118;

template <typename Message> std::vector<uint8_t> serialize_helper(const Message& message) {
    uint8_t serialization_buffer[2048];
    io::StreamOutputView out({serialization_buffer, sizeof(serialization_buffer)});

    const auto size = message_2::serialize(message, out);

    return std::vector<uint8_t>(serialization_buffer, serialization_buffer + size);
}

inline iso2_exiDocument decode_helper(std::vector<uint8_t> bytes) {
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, bytes.data(), bytes.size(), 0, nullptr);

    iso2_exiDocument doc;
    REQUIRE(decode_iso2_exiDocument(&stream, &doc) == 0);
    return doc;
}

// The SECC never decodes a response, so the tests assemble it from the cbv2g body the way an EV would.
template <typename Response, typename CbResponse>
Response to_response(const iso2_exiDocument& doc, const CbResponse& cb_response) {
    Response response;
    message_2::convert(doc.V2G_Message.Header, response.header);
    message_2::convert(cb_response, response);
    return response;
}
