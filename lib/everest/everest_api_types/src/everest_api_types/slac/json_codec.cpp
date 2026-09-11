// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "slac/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "slac/API.hpp"
#include "slac/codec.hpp"
#include <stdexcept>
#include <string>

namespace everest::lib::API::V1_0::types::slac {

void from_json(json const& j, State& k) {
    std::string s = j;
    if (s == "UNMATCHED") {
        k = State::UNMATCHED;
        return;
    }
    if (s == "MATCHING") {
        k = State::MATCHING;
        return;
    }
    if (s == "MATCHED") {
        k = State::MATCHED;
        return;
    }

    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type API_V1_0_TYPES_SLAC_STATE");
}

void to_json(json& j, State const& k) noexcept {
    switch (k) {
    case State::UNMATCHED:
        j = "UNMATCHED";
        return;
    case State::MATCHING:
        j = "MATCHING";
        return;
    case State::MATCHED:
        j = "MATCHED";
        return;
    }

    j = "INVALID_VALUE__everest::lib::API::V1_0::types::SLAC::STATE";
}

void to_json(json& j, ErrorEnum const& k) noexcept {
    switch (k) {
    case ErrorEnum::CommunicationFault:
        j = "CommunicationFault";
        return;
    case ErrorEnum::VendorError:
        j = "VendorError";
        return;
    case ErrorEnum::VendorWarning:
        j = "VendorWarning";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::slac::ErrorEnum";
}

void from_json(json const& j, ErrorEnum& k) {
    std::string s = j;
    if (s == "CommunicationFault") {
        k = ErrorEnum::CommunicationFault;
        return;
    }
    if (s == "VendorError") {
        k = ErrorEnum::VendorError;
        return;
    }
    if (s == "VendorWarning") {
        k = ErrorEnum::VendorWarning;
        return;
    }
    throw std::out_of_range("Provided string " + s + " could not be converted to enum of type ErrorEnum_API_1_0");
}

void to_json(json& j, const Error& k) noexcept {
    j = json{
        {"type", k.type},
    };
    if (k.sub_type) {
        j["sub_type"] = k.sub_type.value();
    }
    if (k.message) {
        j["message"] = k.message.value();
    };
}

void from_json(const json& j, Error& k) {
    k.type = j.at("type");
    if (j.contains("sub_type")) {
        k.sub_type.emplace(j.at("sub_type"));
    }
    if (j.contains("message")) {
        k.message.emplace(j.at("message"));
    }
}


} // namespace everest::lib::API::V1_0::types::slac
