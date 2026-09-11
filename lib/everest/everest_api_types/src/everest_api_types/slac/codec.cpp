// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "slac/codec.hpp"
#include "nlohmann/json.hpp"
#include "slac/API.hpp"
#include "slac/json_codec.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::slac {

std::string serialize(State val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ErrorEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Error const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, State const& val) {
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

template <> State deserialize(std::string_view val) {
    return utilities::parse_json<State>(val);
}

template <> ErrorEnum deserialize(std::string_view val) {
    return utilities::parse_json<ErrorEnum>(val);
}

template <> Error deserialize(std::string_view val) {
    return utilities::parse_json<Error>(val);
}

} // namespace everest::lib::API::V1_0::types::slac
