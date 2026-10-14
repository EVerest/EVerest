// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "generic/json_codec.hpp"
#include "generic/API.hpp"
#include "nlohmann/json.hpp"
#include <stdexcept>
#include <string>

namespace everest::lib::API::V1_0::types::generic {

void to_json(json& j, RequestReply const& k) {
    json payload;
    try {
        payload = json::parse(k.payload);
    } catch (...) {
        throw std::invalid_argument("payload is invalid json: " + k.payload);
    }
    j["headers"]["replyTo"] = k.replyTo;
    j["payload"] = payload;
}

void from_json(json const& j, RequestReply& k) {
    if (j.contains("headers") and j["headers"].contains("replyTo")) {
        k.replyTo = j["headers"]["replyTo"];
    } else {
        k.replyTo = "";
    }

    if (j.contains("payload")) {
        k.payload = j["payload"].dump();
    } else {
        k.payload = "";
    }
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
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::generic::ErrorEnum";
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
    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type API_V1_0_TYPES_GENERIC_ErrorEnum");
}

void to_json(json& j, const CausedByValue& k) noexcept {
    j = json::object();
    if (k.actual_value) {
        j["actual_value"] = k.actual_value.value();
    }
}

void from_json(const json& j, CausedByValue& k) {
    if (j.contains("actual_value")) {
        k.actual_value.emplace(j.at("actual_value"));
    }
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
    if (k.caused_by_value) {
        j["caused_by_value"] = k.caused_by_value.value();
    }
}

void from_json(const json& j, Error& k) {
    k.type = j.at("type");
    if (j.contains("sub_type")) {
        k.sub_type.emplace(j.at("sub_type"));
    }
    if (j.contains("message")) {
        k.message.emplace(j.at("message"));
    }
    if (j.contains("caused_by_value")) {
        k.caused_by_value.emplace(j.at("caused_by_value").get<CausedByValue>());
    }
}

} // namespace everest::lib::API::V1_0::types::generic
