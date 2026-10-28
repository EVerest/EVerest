// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest_api_module_helpers/ValidatingMqttProxy.hpp>
#include <everest_api_types/utilities/MqttProviderInterface.hpp>

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace {

using everest::lib::API::Mqtt::MqttProviderInterface;
using everest::lib::API::Mqtt::ValidatingMqttProxy;

// fails to compile if the overrides in ValidatingMqttProxy hide the const char* overload
[[maybe_unused]] constexpr auto proxy_publishes_string_literals =
    static_cast<void (ValidatingMqttProxy::*)(const std::string&, const char*)>(&ValidatingMqttProxy::publish);

// fails to compile if the overrides in ValidatingMqttProxy hide the overload without data
[[maybe_unused]] constexpr auto proxy_publishes_without_data =
    static_cast<void (ValidatingMqttProxy::*)(const std::string&)>(&ValidatingMqttProxy::publish);

class RecordingMqttProvider : public MqttProviderInterface {
public:
    using MqttProviderInterface::publish;

    void publish(const std::string& topic, const std::string& data) override {
        last_topic = topic;
        last_string = data;
        string_calls++;
    }

    void publish(const std::string& topic, bool data) override {
        last_topic = topic;
        last_bool = data;
        bool_calls++;
    }

    Everest::UnsubscribeToken subscribe(const std::string&, std::function<void(std::string)>) const override {
        return {};
    }

    std::string last_topic;
    std::string last_string;
    bool last_bool{false};
    int string_calls{0};
    int bool_calls{0};
};

TEST(MqttProviderInterface, string_literal_is_published_as_string) {
    RecordingMqttProvider provider;
    MqttProviderInterface& mqtt = provider;

    mqtt.publish("topic", "null");

    EXPECT_EQ(provider.string_calls, 1);
    EXPECT_EQ(provider.bool_calls, 0);
    EXPECT_EQ(provider.last_topic, "topic");
    EXPECT_EQ(provider.last_string, "null");
}

TEST(MqttProviderInterface, empty_string_literal_is_published_as_string) {
    RecordingMqttProvider provider;

    provider.publish("topic", "");

    EXPECT_EQ(provider.string_calls, 1);
    EXPECT_EQ(provider.bool_calls, 0);
    EXPECT_EQ(provider.last_string, "");
}

TEST(MqttProviderInterface, publish_without_data_sends_empty_object) {
    RecordingMqttProvider provider;
    MqttProviderInterface& mqtt = provider;

    mqtt.publish("topic");

    EXPECT_EQ(provider.string_calls, 1);
    EXPECT_EQ(provider.bool_calls, 0);
    EXPECT_EQ(provider.last_topic, "topic");
    EXPECT_EQ(provider.last_string, "{}");
}

TEST(MqttProviderInterface, reply_is_published_on_reply_to) {
    RecordingMqttProvider provider;
    MqttProviderInterface& mqtt = provider;

    mqtt.publish_reply(std::optional<std::string>{"reply/topic"}, "null");
    mqtt.publish_reply(std::optional<std::string>{"reply/topic"}, true);

    EXPECT_EQ(provider.string_calls, 1);
    EXPECT_EQ(provider.bool_calls, 1);
    EXPECT_EQ(provider.last_topic, "reply/topic");
    EXPECT_EQ(provider.last_string, "null");
}

TEST(MqttProviderInterface, reply_without_reply_to_is_discarded) {
    RecordingMqttProvider provider;
    MqttProviderInterface& mqtt = provider;

    mqtt.publish_reply(std::nullopt, "null");
    mqtt.publish_reply(std::nullopt, true);

    EXPECT_EQ(provider.string_calls, 0);
    EXPECT_EQ(provider.bool_calls, 0);
}

TEST(MqttProviderInterface, bool_is_published_as_bool) {
    RecordingMqttProvider provider;
    MqttProviderInterface& mqtt = provider;

    mqtt.publish("topic", true);

    EXPECT_EQ(provider.string_calls, 0);
    EXPECT_EQ(provider.bool_calls, 1);
    EXPECT_TRUE(provider.last_bool);
}

} // namespace
