// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "generic/codec.hpp"
#include "generic/API.hpp"
#include "generic/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

using json = nlohmann::json;

namespace everest::lib::API::V1_0::types::generic {

std::string serialize(bool val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(int val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(size_t val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(double val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(float val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(std::string const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(RequestReply const& val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ErrorEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Error const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, RequestReply const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, const ErrorEnum& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, const Error& val) {
    os << serialize(val);
    return os;
}

template <> bool deserialize(std::string_view val) {
    return utilities::parse_json<bool>(val);
}

template <> int deserialize(std::string_view val) {
    return utilities::parse_json<int>(val);
}

template <> size_t deserialize(std::string_view val) {
    return utilities::parse_json<size_t>(val);
}

template <> double deserialize(std::string_view val) {
    return utilities::parse_json<double>(val);
}

template <> float deserialize(std::string_view val) {
    return utilities::parse_json<float>(val);
}

template <> std::string deserialize(std::string_view val) {
    return utilities::parse_json<std::string>(val);
}

template <> RequestReply deserialize(std::string_view val) {
    return utilities::parse_json<RequestReply>(val);
}

template <> ErrorEnum deserialize(std::string_view val) {
    return utilities::parse_json<ErrorEnum>(val);
}

template <> Error deserialize(std::string_view val) {
    return utilities::parse_json<Error>(val);
}

} // namespace everest::lib::API::V1_0::types::generic
