// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <utils/telemetry/wire.hpp>

namespace everest::telemetry::wire {

namespace {

namespace keys {
constexpr auto TYPE = "t";
constexpr auto SAMPLE_MODULE = "m";
constexpr auto SAMPLE_ELEMENT = "e";
constexpr auto SAMPLE_TIMESTAMP = "ts";
constexpr auto SAMPLE_VALUE = "v";
constexpr auto DECLARE_MODULE = "module";
constexpr auto DECLARE_MODULE_TYPE = "module_type";
constexpr auto DECLARE_ELEMENTS = "elements";
} // namespace keys

constexpr auto SAMPLE_TYPE = "s";
constexpr auto DECLARE_TYPE = "d";

bool is_string(const nlohmann::json& message, const char* key) {
    const auto it = message.find(key);
    return it != message.end() and it->is_string();
}

std::optional<Sample> decode_sample(const nlohmann::json& message) {
    const auto timestamp = message.find(keys::SAMPLE_TIMESTAMP);
    const auto value = message.find(keys::SAMPLE_VALUE);
    if (not is_string(message, keys::SAMPLE_MODULE) or not is_string(message, keys::SAMPLE_ELEMENT) or
        timestamp == message.end() or not timestamp->is_number_integer() or value == message.end()) {
        return std::nullopt;
    }
    return Sample{message.at(keys::SAMPLE_MODULE).get<std::string>(),
                  message.at(keys::SAMPLE_ELEMENT).get<std::string>(), timestamp->get<std::int64_t>(), *value};
}

std::optional<Declare> decode_declare(const nlohmann::json& message) {
    if (not is_string(message, keys::DECLARE_MODULE)) {
        return std::nullopt;
    }
    Declare declare;
    declare.module_id = message.at(keys::DECLARE_MODULE).get<std::string>();
    if (is_string(message, keys::DECLARE_MODULE_TYPE)) {
        declare.module_type = message.at(keys::DECLARE_MODULE_TYPE).get<std::string>();
    }
    declare.elements = parse_element_declarations(message.value(keys::DECLARE_ELEMENTS, nlohmann::json::object()),
                                                  declare.invalid_elements);
    return declare;
}

} // namespace

std::vector<std::uint8_t> encode_payload(const nlohmann::json& payload) {
    const auto text = payload.dump();
    std::vector<std::uint8_t> datagram;
    datagram.reserve(HEADER_SIZE + text.size());
    datagram.push_back(MAGIC_0);
    datagram.push_back(MAGIC_1);
    datagram.push_back(VERSION);
    datagram.push_back(static_cast<std::uint8_t>(Codec::Json));
    datagram.insert(datagram.end(), text.begin(), text.end());
    return datagram;
}

std::vector<std::uint8_t> encode(const Sample& sample) {
    return encode_payload({{keys::TYPE, SAMPLE_TYPE},
                           {keys::SAMPLE_MODULE, sample.module_id},
                           {keys::SAMPLE_ELEMENT, sample.element},
                           {keys::SAMPLE_TIMESTAMP, sample.timestamp_ms},
                           {keys::SAMPLE_VALUE, sample.value}});
}

std::vector<std::uint8_t> encode(const Declare& declare) {
    return encode_payload({{keys::TYPE, DECLARE_TYPE},
                           {keys::DECLARE_MODULE, declare.module_id},
                           {keys::DECLARE_MODULE_TYPE, declare.module_type},
                           {keys::DECLARE_ELEMENTS, declare.elements}});
}

DecodeResult decode(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr or size < HEADER_SIZE) {
        return {std::nullopt, DecodeError::TooShort};
    }
    if (data[0] != MAGIC_0 or data[1] != MAGIC_1) {
        return {std::nullopt, DecodeError::BadMagic};
    }
    if (data[2] != VERSION) {
        return {std::nullopt, DecodeError::UnsupportedVersion};
    }
    if (data[3] != static_cast<std::uint8_t>(Codec::Json)) {
        return {std::nullopt, DecodeError::UnsupportedCodec};
    }
    const auto message = nlohmann::json::parse(data + HEADER_SIZE, data + size, nullptr, false);
    if (message.is_discarded() or not message.is_object()) {
        return {std::nullopt, DecodeError::MalformedPayload};
    }

    const auto type = message.value(keys::TYPE, std::string{});
    if (type == SAMPLE_TYPE) {
        auto sample = decode_sample(message);
        return sample.has_value() ? DecodeResult{Message{std::move(sample.value())}, std::nullopt}
                                  : DecodeResult{std::nullopt, DecodeError::MalformedPayload};
    }
    if (type == DECLARE_TYPE) {
        auto declare = decode_declare(message);
        return declare.has_value() ? DecodeResult{Message{std::move(declare.value())}, std::nullopt}
                                   : DecodeResult{std::nullopt, DecodeError::MalformedPayload};
    }
    return {std::nullopt, DecodeError::UnknownMessageType};
}

std::string_view to_string(DecodeError error) {
    switch (error) {
    case DecodeError::TooShort:
        return "too short";
    case DecodeError::BadMagic:
        return "bad magic";
    case DecodeError::UnsupportedVersion:
        return "unsupported version";
    case DecodeError::UnsupportedCodec:
        return "unsupported codec";
    case DecodeError::MalformedPayload:
        return "malformed payload";
    case DecodeError::UnknownMessageType:
        return "unknown message type";
    }
    return "unknown";
}

} // namespace everest::telemetry::wire
