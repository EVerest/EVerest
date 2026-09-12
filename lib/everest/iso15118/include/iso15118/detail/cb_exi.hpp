// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>

#include <cbv2g/common/exi_bitstream.h>

#include <iso15118/io/stream_view.hpp>

#define CB2CPP_STRING(property) (std::string(property.characters, property.charactersLen))

#define CPP2CB_STRING(in, out)                                                                                         \
    if (in.length() > sizeof(out.characters)) {                                                                        \
        throw std::runtime_error("String too long");                                                                   \
    }                                                                                                                  \
    in.copy(out.characters, in.length());                                                                              \
    out.charactersLen = in.length()

#define CPP2CB_BYTES(in, out)                                                                                          \
    if (in.size() > sizeof(out.bytes)) {                                                                               \
        throw std::runtime_error("Byte vector too long");                                                              \
    }                                                                                                                  \
    std::copy(in.begin(), in.end(), out.bytes);                                                                        \
    out.bytesLen = in.size()

#define CB2CPP_BYTES(in, out)                                                                                          \
    if (in.bytesLen > out.size()) {                                                                                    \
        throw std::runtime_error("Byte array too long");                                                               \
    }                                                                                                                  \
    std::copy_n(std::begin(in.bytes), in.bytesLen, std::begin(out));

// Guards an encode loop that copies a C++ container into a fixed-size cbv2g C array. Throws (rather than
// writing past the array) when the source has more elements than the generated array can hold.
#define CPP2CB_ARRAY_SIZE_CHECK(count, out_array)                                                                      \
    if ((count) > (sizeof(out_array) / sizeof((out_array)[0]))) {                                                      \
        throw std::runtime_error("Too many elements for target array");                                                \
    }

#define CB_SET_USED(property) (property##_isUsed = 1)
#define CB2CPP_ASSIGN_IF_USED(in, out)                                                                                 \
    if (in##_isUsed) {                                                                                                 \
        out = in;                                                                                                      \
    }

#define CPP2CB_ASSIGN_IF_USED(in, out)                                                                                 \
    if (in) {                                                                                                          \
        out = in.value();                                                                                              \
    }                                                                                                                  \
                                                                                                                       \
    out##_isUsed = static_cast<bool>(in);

#define CB2CPP_CONVERT_IF_USED(in, out)                                                                                \
    if (in##_isUsed) {                                                                                                 \
        convert(in, out.emplace());                                                                                    \
    }

#define CPP2CB_CONVERT_IF_USED(in, out)                                                                                \
    if (in) {                                                                                                          \
        convert(in.value(), out);                                                                                      \
    }                                                                                                                  \
                                                                                                                       \
    out##_isUsed = static_cast<bool>(in);

#define CPP2CB_STRING_IF_USED(in, out)                                                                                 \
    if (in) {                                                                                                          \
        CPP2CB_STRING(in.value(), out);                                                                                \
    }                                                                                                                  \
                                                                                                                       \
    out##_isUsed = static_cast<bool>(in);

#define CB2CPP_STRING_IF_USED(in, out)                                                                                 \
    if (in##_isUsed) {                                                                                                 \
        out = CB2CPP_STRING(in);                                                                                       \
    }

#define CB2CPP_BYTES_IF_USED(in, out)                                                                                  \
    if (in##_isUsed) {                                                                                                 \
        CB2CPP_BYTES(in, out.emplace());                                                                               \
    }

template <typename T1, typename T2> void cb_convert_enum(const T1& in, T2& out) {
    out = static_cast<T2>(in);
}

exi_bitstream_t get_exi_input_stream(const iso15118::io::StreamInputView&);
exi_bitstream_t get_exi_output_stream(const iso15118::io::StreamOutputView&);

namespace iso15118 {

// Shared EXI serialization helper for every protocol namespace (message_2, message_din, message_20).
// The unqualified serialize_to_exi() call is a dependent name resolved via ADL to the per-protocol
// overload set (and explicit specializations) of the argument's namespace at instantiation time.
template <typename MessageType>
size_t serialize_helper(const MessageType& in, const io::StreamOutputView& stream_view) {
    auto out = get_exi_output_stream(stream_view);

    const auto error = serialize_to_exi(in, out);

    if (error != 0) {
        throw std::runtime_error("Could not encode exi: " + std::to_string(error));
    }

    return exi_bitstream_get_length(&out);
}

} // namespace iso15118

namespace iso15118::message_20 {

template <typename MessageType> int serialize_to_exi(const MessageType& in, exi_bitstream_t& out);

} // namespace iso15118::message_20
