// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "session_storage/codec.hpp"
#include "nlohmann/json.hpp"
#include "session_storage/API.hpp"
#include "session_storage/json_codec.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::session_storage {

std::string serialize(SessionState val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Transaction const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Session const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SessionFilter const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(GetSessionsRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SessionList const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SessionIdentifier const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ClearSessionsResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, SessionState const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Transaction const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Session const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SessionFilter const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, GetSessionsRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SessionList const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SessionIdentifier const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ClearSessionsResult const& val) {
    os << serialize(val);
    return os;
}

template <> SessionState deserialize(std::string_view val) {
    return utilities::parse_json<SessionState>(val);
}

template <> Transaction deserialize(std::string_view val) {
    return utilities::parse_json<Transaction>(val);
}

template <> Session deserialize(std::string_view val) {
    return utilities::parse_json<Session>(val);
}

template <> SessionFilter deserialize(std::string_view val) {
    return utilities::parse_json<SessionFilter>(val);
}

template <> GetSessionsRequest deserialize(std::string_view val) {
    return utilities::parse_json<GetSessionsRequest>(val);
}

template <> SessionList deserialize(std::string_view val) {
    return utilities::parse_json<SessionList>(val);
}

template <> SessionIdentifier deserialize(std::string_view val) {
    return utilities::parse_json<SessionIdentifier>(val);
}

template <> ClearSessionsResult deserialize(std::string_view val) {
    return utilities::parse_json<ClearSessionsResult>(val);
}

} // namespace everest::lib::API::V1_0::types::session_storage
