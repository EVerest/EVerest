// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "error_history/codec.hpp"
#include "error_history/API.hpp"
#include "error_history/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::error_history {

std::string serialize(State val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SeverityFilter val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Severity val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ImplementationIdentifier const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(TimeperiodFilter const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(FilterArguments const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ErrorObject const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ErrorList const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, State const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SeverityFilter const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Severity const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ImplementationIdentifier const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, TimeperiodFilter const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, FilterArguments const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ErrorObject const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ErrorList const& val) {
    os << serialize(val);
    return os;
}

template <> State deserialize(std::string_view val) {
    return utilities::parse_json<State>(val);
}

template <> SeverityFilter deserialize(std::string_view val) {
    return utilities::parse_json<SeverityFilter>(val);
}

template <> Severity deserialize(std::string_view val) {
    return utilities::parse_json<Severity>(val);
}

template <> ImplementationIdentifier deserialize(std::string_view val) {
    return utilities::parse_json<ImplementationIdentifier>(val);
}

template <> TimeperiodFilter deserialize(std::string_view val) {
    return utilities::parse_json<TimeperiodFilter>(val);
}

template <> FilterArguments deserialize(std::string_view val) {
    return utilities::parse_json<FilterArguments>(val);
}

template <> ErrorObject deserialize(std::string_view val) {
    return utilities::parse_json<ErrorObject>(val);
}

template <> ErrorList deserialize(std::string_view val) {
    return utilities::parse_json<ErrorList>(val);
}

} // namespace everest::lib::API::V1_0::types::error_history
