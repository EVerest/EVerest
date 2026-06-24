// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "auth/codec.hpp"
#include "auth/API.hpp"
#include "auth/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::auth {

std::string serialize(AuthorizationStatus val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(CertificateStatus val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(TokenValidationStatus val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SelectionAlgorithm val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(AuthorizationType val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(IdTokenType val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(CustomIdToken const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(IdToken const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(WithdrawAuthorizationResult val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ProvidedIdToken const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(TokenValidationStatusMessage const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ValidationResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ValidationResultUpdate const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(WithdrawAuthorizationRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, AuthorizationStatus const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, CertificateStatus const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, TokenValidationStatus const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SelectionAlgorithm const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, AuthorizationType const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, IdTokenType const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, WithdrawAuthorizationResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, CustomIdToken const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, IdToken const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ProvidedIdToken const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, TokenValidationStatusMessage const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ValidationResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ValidationResultUpdate const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, WithdrawAuthorizationRequest const& val) {
    os << serialize(val);
    return os;
}

template <> AuthorizationStatus deserialize(std::string_view val) {
    return utilities::parse_json<AuthorizationStatus>(val);
}

template <> CertificateStatus deserialize(std::string_view val) {
    return utilities::parse_json<CertificateStatus>(val);
}

template <> TokenValidationStatus deserialize(std::string_view val) {
    return utilities::parse_json<TokenValidationStatus>(val);
}

template <> SelectionAlgorithm deserialize(std::string_view val) {
    return utilities::parse_json<SelectionAlgorithm>(val);
}

template <> AuthorizationType deserialize(std::string_view val) {
    return utilities::parse_json<AuthorizationType>(val);
}

template <> IdTokenType deserialize(std::string_view val) {
    return utilities::parse_json<IdTokenType>(val);
}

template <> WithdrawAuthorizationResult deserialize(std::string_view val) {
    return utilities::parse_json<WithdrawAuthorizationResult>(val);
}

template <> CustomIdToken deserialize(std::string_view val) {
    return utilities::parse_json<CustomIdToken>(val);
}

template <> IdToken deserialize(std::string_view val) {
    return utilities::parse_json<IdToken>(val);
}

template <> ProvidedIdToken deserialize(std::string_view val) {
    return utilities::parse_json<ProvidedIdToken>(val);
}

template <> TokenValidationStatusMessage deserialize(std::string_view val) {
    return utilities::parse_json<TokenValidationStatusMessage>(val);
}

template <> ValidationResult deserialize(std::string_view val) {
    return utilities::parse_json<ValidationResult>(val);
}

template <> ValidationResultUpdate deserialize(std::string_view val) {
    return utilities::parse_json<ValidationResultUpdate>(val);
}

template <> WithdrawAuthorizationRequest deserialize(std::string_view val) {
    return utilities::parse_json<WithdrawAuthorizationRequest>(val);
}

} // namespace everest::lib::API::V1_0::types::auth
