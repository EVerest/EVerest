// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <iso15118/io/stream_view.hpp>
#include <iso15118/message_din/type.hpp>
#include <iso15118/message_din/variant.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefDecoder.h>

using namespace iso15118;

template <typename Message> std::vector<uint8_t> serialize_helper(const Message& message) {
    uint8_t serialization_buffer[1024];
    io::StreamOutputView out({serialization_buffer, sizeof(serialization_buffer)});

    const auto size = message_din::serialize(message, out);

    return std::vector<uint8_t>(serialization_buffer, serialization_buffer + size);
}

// Serialize and decode back. The two convert() directions are written by hand and
// independently, so a mismatch between them shows up here.
template <typename Message> message_din::Variant roundtrip(const Message& message) {
    const auto buffer = serialize_helper(message);

    return message_din::Variant{io::StreamInputView{buffer.data(), buffer.size()}};
}

// Decode with the raw cbv2g C API rather than the library's own Variant, so the wire form is
// checked against an oracle the library does not share. Needed for fields the domain types
// drop, such as the physical value Unit.
inline din_exiDocument decode_raw(const std::vector<uint8_t>& bytes) {
    exi_bitstream_t stream;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    exi_bitstream_init(&stream, const_cast<uint8_t*>(bytes.data()), bytes.size(), 0, nullptr);

    din_exiDocument doc{};
    REQUIRE(decode_din_exiDocument(&stream, &doc) == 0);

    return doc;
}
