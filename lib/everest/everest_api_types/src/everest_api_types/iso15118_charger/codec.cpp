// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "iso15118_charger/codec.hpp"
#include "iso15118_charger/API.hpp"
#include "iso15118_charger/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::iso15118_charger {

std::string serialize(CertificateActionEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(EnergyTransferMode val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Status val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(HashAlgorithm val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(RequestExiStreamSchema const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ResponseExiStreamStatus const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(CertificateHashDataInfo const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(EnergyTransferModeList const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, CertificateActionEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EnergyTransferMode const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Status const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, HashAlgorithm const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, RequestExiStreamSchema const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ResponseExiStreamStatus const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, CertificateHashDataInfo const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EnergyTransferModeList const& val) {
    os << serialize(val);
    return os;
}

template <> CertificateActionEnum deserialize(std::string_view val) {
    return utilities::parse_json<CertificateActionEnum>(val);
}

template <> EnergyTransferMode deserialize(std::string_view val) {
    return utilities::parse_json<EnergyTransferMode>(val);
}

template <> Status deserialize(std::string_view val) {
    return utilities::parse_json<Status>(val);
}

template <> HashAlgorithm deserialize(std::string_view val) {
    return utilities::parse_json<HashAlgorithm>(val);
}

template <> RequestExiStreamSchema deserialize(std::string_view val) {
    return utilities::parse_json<RequestExiStreamSchema>(val);
}

template <> ResponseExiStreamStatus deserialize(std::string_view val) {
    return utilities::parse_json<ResponseExiStreamStatus>(val);
}

template <> CertificateHashDataInfo deserialize(std::string_view val) {
    return utilities::parse_json<CertificateHashDataInfo>(val);
}

template <> EnergyTransferModeList deserialize(std::string_view val) {
    return utilities::parse_json<EnergyTransferModeList>(val);
}

} // namespace everest::lib::API::V1_0::types::iso15118_charger
