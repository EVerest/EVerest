// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include <utils/telemetry/catalog.hpp>

/// Datagram format shared by telemetry producers and the telemetry receiver.
///
/// Every datagram starts with a four byte header: the magic bytes 'E' 'T', the protocol version and a codec
/// byte. The payload is a JSON object whose "t" member names the message type.
namespace everest::telemetry::wire {

enum class Codec : std::uint8_t {
    Json = 1,
};

inline constexpr std::uint8_t MAGIC_0 = 'E';
inline constexpr std::uint8_t MAGIC_1 = 'T';
inline constexpr std::uint8_t VERSION = 1;
inline constexpr std::size_t HEADER_SIZE = 4;
inline constexpr std::size_t MAX_DATAGRAM_SIZE = 64 * 1024;

/// \brief One value of a telemetry element; sent on every handle call
struct Sample {
    std::string module_id;
    std::string element;
    std::int64_t timestamp_ms{0};
    /// Serialized value of the element: number, counter total, boolean, string or object
    nlohmann::json value;
};

/// \brief Elements of a producer that has no manifest in the configuration
struct Declare {
    std::string module_id;
    std::string module_type;
    ElementDeclarations elements;
    /// Elements that could not be parsed when decoding; they are not part of elements
    std::vector<std::string> invalid_elements;
};

using Message = std::variant<Sample, Declare>;

enum class DecodeError {
    TooShort,
    BadMagic,
    UnsupportedVersion,
    UnsupportedCodec,
    MalformedPayload,
    UnknownMessageType,
};

struct DecodeResult {
    std::optional<Message> message;
    std::optional<DecodeError> error;
};

std::vector<std::uint8_t> encode(const Sample& sample);
std::vector<std::uint8_t> encode(const Declare& declare);

/// \brief Header plus the JSON encoding of an arbitrary payload, e.g. to model other senders in tests
std::vector<std::uint8_t> encode_payload(const nlohmann::json& payload);

/// \brief Parse one datagram
DecodeResult decode(const std::uint8_t* data, std::size_t size);

std::string_view to_string(DecodeError error);

} // namespace everest::telemetry::wire
