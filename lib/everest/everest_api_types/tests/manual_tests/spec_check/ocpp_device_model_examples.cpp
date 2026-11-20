// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest_api_types/ocpp/codec.hpp>
#include <everest_api_types/ocpp_device_model/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>

#include <everest/utils/yaml_loader.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace {

namespace fs = std::filesystem;
namespace dm = everest::lib::API::V1_0::types::ocpp_device_model;
namespace ocpp = everest::lib::API::V1_0::types::ocpp;
using everest::lib::API::deserialize;
using nlohmann::json;

// Parses a payload into its API type and returns it serialised again, or nullopt if parsing fails.
using Parser = std::function<std::optional<std::string>(std::string const&)>;

template <class T> Parser parser() {
    return [](std::string const& data) -> std::optional<std::string> {
        T obj;
        if (not deserialize(data, obj)) {
            return std::nullopt;
        }
        return serialize(obj);
    };
}

template <class T> Parser primitive_parser() {
    return [](std::string const& data) -> std::optional<std::string> {
        T obj;
        if (not deserialize(data, obj)) {
            return std::nullopt;
        }
        return json(obj).dump();
    };
}

struct MessageType {
    Parser parse;
    bool request_with_headers{false};
};

const std::map<std::string, MessageType>& message_types() {
    static const std::map<std::string, MessageType> types{
        {"receive_get_registrations", {parser<dm::GetRegistrations>()}},
        {"receive_set_registration_result", {parser<dm::RegistrationResult>()}},
        {"receive_request_set_value", {parser<dm::WriteRequest>(), true}},
        {"send_reply_set_value", {parser<ocpp::SetVariableStatusEnumType>()}},
        {"send_registration", {parser<dm::Registration>()}},
        {"send_value_changed", {parser<dm::ValueChange>()}},
        {"receive_heartbeat", {primitive_parser<std::size_t>()}},
        {"send_communication_check", {primitive_parser<bool>()}},
    };
    return types;
}

json load_spec() {
    const fs::path spec{fs::path{EVEREST_API_SPEC_DIR} / "ocpp_device_model_API.yaml"};
    EXPECT_TRUE(fs::is_regular_file(spec)) << spec;
    auto doc = json::parse(Everest::load_yaml(spec).dump());
    // a file starting with "---" is loaded as a stream of documents
    if (doc.is_array() and doc.size() == 1) {
        doc = doc.at(0);
    }
    return doc;
}

TEST(OcppDeviceModelApiSpec, every_message_has_an_api_type) {
    const auto spec = load_spec();
    const auto& messages = spec.at("components").at("messages");
    ASSERT_FALSE(messages.empty());
    for (const auto& [name, message] : messages.items()) {
        EXPECT_EQ(message_types().count(name), 1U) << "no API type for message " << name;
    }
}

TEST(OcppDeviceModelApiSpec, every_example_parses_into_its_api_type_and_back) {
    const auto spec = load_spec();
    std::size_t examples = 0;
    for (const auto& [name, message] : spec.at("components").at("messages").items()) {
        const auto type = message_types().find(name);
        if (type == message_types().end()) {
            continue;
        }
        for (const auto& example : message.value("examples", json::array())) {
            SCOPED_TRACE(name + ": " + example.value("summary", std::string{}));
            auto payload = example.at("payload");
            if (type->second.request_with_headers) {
                ASSERT_TRUE(payload.at("headers").at("replyTo").is_string());
                payload = payload.at("payload");
            }
            const auto reserialized = type->second.parse(payload.dump());
            ASSERT_TRUE(reserialized.has_value()) << payload.dump();
            EXPECT_EQ(json::parse(reserialized.value()), payload);
            ++examples;
        }
    }
    EXPECT_GT(examples, 0U);
}

} // namespace
