// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <utils/mqtt_abstraction.hpp>
#include <utils/mqtt_abstraction_impl.hpp>
#include <utils/mqtt_framework_transport.hpp>
#include <utils/shm_framework_transport.hpp>

#include <tests/mock_framework_transport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <vector>

#include <everest/exceptions.hpp>
#include <everest/io/shm/ring_buffer.hpp>
#include <everest/io/shm/shared_memory.hpp>
#include <everest/io/shm/shm_client.hpp>
#include <everest/io/shm/shm_server.hpp>
#include <everest/io/shm/structures.hpp>

using namespace std::chrono_literals;

namespace {

namespace shm = everest::lib::io::shm;

constexpr auto timeout = 3s;

bool uds_bind_unavailable(const shm::io_result& result) {
    return result.status == shm::io_status::resource_error &&
           result.message.find("Operation not permitted") != std::string::npos &&
           result.message.find("bind UDS server") != std::string::npos;
}

shm::server_options make_server_options(std::string suffix) {
    shm::server_options options;
    options.shm_name = "/everest-framework-shm-" + suffix;
    options.control_socket_name = "/tmp/everest-framework-shm-control-" + std::to_string(::getpid()) + "-" + suffix;
    options.control_socket_abstract_namespace = false;
    options.topics.push_back({"everest/modules/source/impl/main/var/session", 4, 512});
    options.topics.push_back({"everest/modules/source/impl/main/var/raw", 4, 512});
    options.topics.push_back({"everest/modules/source/impl/main/var/qos-retain", 4, 512});
    options.topics.push_back({"external/source/a", 4, 512});
    options.topics.push_back({"external/source/b", 4, 512});
    options.topics.push_back({"external/source/qos0", 4, 512});
    options.topics.push_back({"external/source/qos1", 4, 512});
    options.topics.push_back({"external/source/qos2", 4, 512});
    options.topics.push_back({"everest/config/request", 8, 2048});
    options.topics.push_back({"everest/modules/test/response", 8, 2048});
    options.topics.push_back({"everest/modules/retry/response", 8, 2048});
    options.topics.push_back({"everest/modules/a/response", 8, 2048});
    options.topics.push_back({"everest/modules/b/response", 8, 2048});
    options.unlink_shm_on_close = true;
    options.unlink_control_socket_on_close = true;
    return options;
}

shm::client_options make_client_options(const shm::server_options& server_options, std::string client_id) {
    shm::client_options options;
    options.client_id = std::move(client_id);
    options.control.server_name = server_options.control_socket_name;
    options.control.server_abstract_namespace = server_options.control_socket_abstract_namespace;
    return options;
}

Everest::MQTTSettings make_mqtt_settings(const shm::server_options& server_options) {
    Everest::MQTTSettings settings;
    settings.broker_socket_path = server_options.control_socket_name;
    settings.everest_prefix = "everest/";
    settings.external_prefix = "external/";
    settings.shm_registered_topics.reserve(server_options.topics.size());
    for (const auto& topic : server_options.topics) {
        settings.shm_registered_topics.push_back(topic.name);
    }
    return settings;
}

Everest::MQTTSettings make_shared_mem_settings(const shm::server_options& server_options) {
    auto settings = make_mqtt_settings(server_options);
    settings.framework_transport = Everest::FrameworkTransportType::SHM;
    settings.shm_control_socket_path = server_options.control_socket_name;
    return settings;
}

template <typename Future> void drive_until_ready(shm::shm_server& server, Future& future) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (future.wait_for(0ms) == std::future_status::ready) {
            return;
        }
        server.sync();
    }
    REQUIRE(future.wait_for(0ms) == std::future_status::ready);
}

void drive_server_and_client_until(shm::shm_server& server, shm::shm_client& client,
                                   const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) {
            return;
        }
        server.sync();
        client.sync();
    }
    REQUIRE(done());
}

void subscribe_with_server(shm::shm_server& server, shm::shm_client& client, const std::string& topic,
                           shm::shm_client::message_callback callback) {
    auto future = std::async(std::launch::async, [&]() { return client.subscribe(topic, std::move(callback)); });
    drive_until_ready(server, future);
    const auto result = future.get();
    REQUIRE(result.status == shm::io_status::ok);
}

template <typename Subscribe> void adapter_subscribe_with_server(shm::shm_server& server, Subscribe subscribe) {
    auto future = std::async(std::launch::async, std::move(subscribe));
    drive_until_ready(server, future);
    future.get();
}

template <typename Publish> void publish_with_server(shm::shm_server& server, Publish publish) {
    auto future = std::async(std::launch::async, std::move(publish));
    drive_until_ready(server, future);
    future.get();
}

bool wait_for_condition(shm::shm_server& server, const std::function<bool()>& done, std::chrono::milliseconds wait) {
    const auto deadline = std::chrono::steady_clock::now() + wait;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) {
            return true;
        }
        server.sync();
        std::this_thread::sleep_for(1ms);
    }
    return done();
}

bool wait_for_value(const std::function<bool()>& done, std::chrono::milliseconds wait) {
    const auto deadline = std::chrono::steady_clock::now() + wait;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return done();
}

shm::ring_buffer ring_for_topic(shm::shared_memory& memory, const std::string& topic) {
    auto* header = static_cast<shm::SegmentHeader*>(memory.get_ptr());
    auto* entries = reinterpret_cast<shm::TopicRegistryEntry*>(static_cast<unsigned char*>(memory.get_ptr()) +
                                                               header->registry_offset);
    for (std::uint32_t i = 0; i < header->registry_entry_count; ++i) {
        const auto& entry = entries[i];
        if ((entry.flags & shm::shm_topic_registry_entry_active) == 0U) {
            continue;
        }
        const std::string entry_topic(reinterpret_cast<const char*>(entry.topic_name), entry.topic_name_length);
        if (entry_topic == topic) {
            return shm::ring_buffer(static_cast<unsigned char*>(memory.get_ptr()) + entry.ring_offset);
        }
    }
    FAIL("SHM topic was not found in registry: " << topic);
    return shm::ring_buffer(memory.get_ptr());
}

constexpr std::array<Everest::QOS, 3> all_qos_values() {
    return {Everest::QOS::QOS0, Everest::QOS::QOS1, Everest::QOS::QOS2};
}

struct open_server {
    shm::server_options options;
    shm::shm_server server;
    bool available{false};

    explicit open_server(std::string suffix) : options(make_server_options(std::move(suffix))), server(options) {
        const auto result = server.open();
        if (uds_bind_unavailable(result)) {
            WARN("SKIP: UDS bind is not permitted in this environment");
            return;
        }
        REQUIRE(result.status == shm::io_status::ok);
        available = true;
    }

    ~open_server() {
        if (available && server.is_open()) {
            (void)server.close();
        }
    }
};

} // namespace

TEST_CASE("FrameworkTransport compatibility aliases compile for old MQTTAbstraction names") {
    static_assert(std::is_same_v<Everest::MQTTAbstraction, Everest::FrameworkTransport>);
    static_assert(std::is_same_v<Everest::MQTTAbstractionImpl, Everest::MqttFrameworkTransport>);

    Everest::MQTTSettings settings;
    settings.broker_socket_path = "/tmp/real-mqtt-broker.sock";
    settings.framework_transport = Everest::FrameworkTransportType::SHM;
    settings.shm_control_socket_path = "/tmp/unused.sock";

    std::unique_ptr<Everest::MQTTAbstraction> transport = Everest::make_mqtt_abstraction(settings);
    REQUIRE(dynamic_cast<Everest::ShmFrameworkTransport*>(transport.get()) != nullptr);
}

TEST_CASE("ShmFrameworkTransport exposes stable prefix references") {
    Everest::MQTTSettings settings;
    settings.framework_transport = Everest::FrameworkTransportType::SHM;
    settings.shm_control_socket_path = "/tmp/unused.sock";
    settings.everest_prefix = "custom/everest/";
    settings.external_prefix = "custom/external/";
    Everest::ShmFrameworkTransport abstraction(settings);

    const auto& everest_prefix = abstraction.get_everest_prefix();
    const auto& external_prefix = abstraction.get_external_prefix();

    REQUIRE(everest_prefix == "custom/everest/");
    REQUIRE(external_prefix == "custom/external/");
    REQUIRE(&everest_prefix == &abstraction.get_everest_prefix());
    REQUIRE(&external_prefix == &abstraction.get_external_prefix());
}

TEST_CASE("MqttFrameworkTransport threadless queue direct drain routes through MessageHandler") {
    Everest::MQTTSettings settings;
    settings.everest_prefix = "everest/";
    settings.external_prefix = "external/";
    settings.broker_socket_path = "/tmp/unused-mqtt-broker.sock";

    Everest::MqttFrameworkTransport transport(settings);

    std::promise<std::pair<std::string, std::string>> received;
    auto received_future = received.get_future();
    const auto handler = std::make_shared<TypedHandler>(
        HandlerType::ExternalMQTT, std::make_shared<Handler>([&](const std::string& topic, json data) {
            received.set_value({topic, data.get<std::string>()});
        }));
    transport.register_handler("external/source/a", handler, Everest::QOS::QOS0);

    REQUIRE(transport.enqueue_transport_message_for_test(Everest::Message("external/source/a", "mqtt-direct")));
    REQUIRE(transport.drain_transport_queue_for_test() == 1U);
    REQUIRE(received_future.wait_for(timeout) == std::future_status::ready);
    REQUIRE(received_future.get() == std::make_pair(std::string("external/source/a"), std::string("mqtt-direct")));
}

TEST_CASE("MqttFrameworkTransport threadless queue wakes existing fd_event_handler") {
    Everest::MQTTSettings settings;
    settings.everest_prefix = "everest/";
    settings.external_prefix = "external/";
    settings.broker_socket_path = "/tmp/unused-mqtt-broker.sock";

    Everest::MqttFrameworkTransport transport(settings);
    REQUIRE(transport.register_transport_queue_events_for_test());

    std::promise<std::string> received;
    auto received_future = received.get_future();
    const auto handler = std::make_shared<TypedHandler>(
        HandlerType::ExternalMQTT,
        std::make_shared<Handler>([&](const std::string&, json data) { received.set_value(data.get<std::string>()); }));
    transport.register_handler("external/source/a", handler, Everest::QOS::QOS0);

    REQUIRE(transport.enqueue_transport_message_for_test(Everest::Message("external/source/a", "mqtt-eventfd")));
    REQUIRE(transport.poll_transport_events_for_test(timeout));
    REQUIRE(received_future.wait_for(timeout) == std::future_status::ready);
    REQUIRE(received_future.get() == "mqtt-eventfd");
    REQUIRE(transport.unregister_transport_queue_events_for_test());
}

TEST_CASE("ShmFrameworkTransport threadless queue direct drain routes through MessageHandler") {
    open_server fixture("threadless-direct");
    if (!fixture.available) {
        return;
    }

    Everest::ShmFrameworkTransport transport(make_mqtt_settings(fixture.options));
    REQUIRE(transport.connect());

    std::promise<std::pair<std::string, std::string>> received;
    auto received_future = received.get_future();
    const auto handler = std::make_shared<TypedHandler>(
        HandlerType::ExternalMQTT, std::make_shared<Handler>([&](const std::string& topic, json data) {
            received.set_value({topic, data.get<std::string>()});
        }));
    adapter_subscribe_with_server(
        fixture.server, [&]() { transport.register_handler("external/source/a", handler, Everest::QOS::QOS0); });

    REQUIRE(transport.enqueue_transport_message_for_test(Everest::Message("external/source/a", "shm-direct")));
    REQUIRE(transport.drain_transport_queue_for_test() == 1U);
    REQUIRE(received_future.wait_for(timeout) == std::future_status::ready);
    REQUIRE(received_future.get() == std::make_pair(std::string("external/source/a"), std::string("shm-direct")));

    transport.disconnect();
}

TEST_CASE("ShmFrameworkTransport threadless queue wakes existing fd_event_handler") {
    open_server fixture("threadless-eventfd");
    if (!fixture.available) {
        return;
    }

    Everest::ShmFrameworkTransport transport(make_mqtt_settings(fixture.options));
    REQUIRE(transport.connect());
    REQUIRE(transport.register_transport_queue_events_for_test());

    std::promise<std::string> received;
    auto received_future = received.get_future();
    const auto handler = std::make_shared<TypedHandler>(
        HandlerType::ExternalMQTT,
        std::make_shared<Handler>([&](const std::string&, json data) { received.set_value(data.get<std::string>()); }));
    adapter_subscribe_with_server(
        fixture.server, [&]() { transport.register_handler("external/source/a", handler, Everest::QOS::QOS0); });

    REQUIRE(transport.enqueue_transport_message_for_test(Everest::Message("external/source/a", "shm-eventfd")));
    REQUIRE(transport.poll_transport_events_for_test(timeout));
    REQUIRE(received_future.wait_for(timeout) == std::future_status::ready);
    REQUIRE(received_future.get() == "shm-eventfd");
    REQUIRE(transport.unregister_transport_queue_events_for_test());

    transport.disconnect();
}

TEST_CASE("ShmFrameworkTransport uses explicit SHM control endpoint") {
    open_server fixture("shared-mem-selector");
    if (!fixture.available) {
        return;
    }

    Everest::ShmFrameworkTransport mqtt(make_shared_mem_settings(fixture.options));

    REQUIRE(mqtt.connect());
    REQUIRE(mqtt.connect());
    REQUIRE_NOTHROW(mqtt.disconnect());
    REQUIRE_NOTHROW(mqtt.disconnect());
}

TEST_CASE("ShmFrameworkTransport ignores MQTT broker host for SHM control endpoint") {
    open_server fixture("shared-mem-control-endpoint");
    if (!fixture.available) {
        return;
    }

    auto settings = make_shared_mem_settings(fixture.options);
    settings.broker_host = "/tmp/everest-framework-shm-unused-" + std::to_string(::getpid()) + ".sock";

    Everest::ShmFrameworkTransport mqtt(settings);

    REQUIRE(mqtt.connect());
    REQUIRE_NOTHROW(mqtt.disconnect());
}

TEST_CASE("ShmFrameworkTransport publishes JSON payloads through SHM") {
    open_server fixture("json");
    if (!fixture.available) {
        return;
    }

    shm::shm_client subscriber(make_client_options(fixture.options, "json-subscriber"));
    REQUIRE(subscriber.connect().status == shm::io_status::ok);

    std::string received_topic;
    std::string received_payload;
    const std::string topic = "everest/modules/source/impl/main/var/session";
    subscribe_with_server(fixture.server, subscriber, topic,
                          [&](std::string_view topic_view, std::string_view payload) {
                              received_topic = std::string(topic_view);
                              received_payload = std::string(payload);
                          });

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    const nlohmann::json payload = {{"value", 42}, {"unit", "A"}};
    publish_with_server(fixture.server, [&]() { mqtt.publish(topic, payload); });
    drive_server_and_client_until(fixture.server, subscriber, [&]() { return !received_payload.empty(); });

    REQUIRE(received_topic == topic);
    REQUIRE(received_payload == payload.dump());
}

TEST_CASE("ShmFrameworkTransport publishes string payload bytes unchanged") {
    open_server fixture("string");
    if (!fixture.available) {
        return;
    }

    shm::shm_client subscriber(make_client_options(fixture.options, "string-subscriber"));
    REQUIRE(subscriber.connect().status == shm::io_status::ok);

    std::string received_topic;
    std::string received_payload;
    const std::string topic = "everest/modules/source/impl/main/var/raw";
    const std::string payload("raw\0payload bytes", 17);
    subscribe_with_server(fixture.server, subscriber, topic, [&](std::string_view topic_view, std::string_view data) {
        received_topic = std::string(topic_view);
        received_payload = std::string(data);
    });

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    publish_with_server(fixture.server, [&]() { mqtt.publish(topic, payload); });
    drive_server_and_client_until(fixture.server, subscriber,
                                  [&]() { return received_payload.size() == payload.size(); });

    REQUIRE(received_topic == topic);
    REQUIRE(received_payload == payload);
}

TEST_CASE("ShmFrameworkTransport publish fails before connect") {
    const auto server_options = make_server_options("before-connect");
    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(server_options));

    REQUIRE_THROWS_WITH(mqtt.publish("everest/modules/source/impl/main/var/raw", std::string("payload")),
                        Catch::Matchers::ContainsSubstring("not_open"));
}

TEST_CASE("ShmFrameworkTransport publish fails after disconnect") {
    const auto server_options = make_server_options("after-disconnect");
    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(server_options));
    REQUIRE(mqtt.connect());
    mqtt.disconnect();

    REQUIRE_THROWS_WITH(mqtt.publish("everest/modules/source/impl/main/var/raw", std::string("payload")),
                        Catch::Matchers::ContainsSubstring("not_open"));
}

TEST_CASE("ShmFrameworkTransport QoS and retain do not alter SHM topic or payload") {
    open_server fixture("qos-retain");
    if (!fixture.available) {
        return;
    }

    shm::shm_client subscriber(make_client_options(fixture.options, "qos-retain-subscriber"));
    REQUIRE(subscriber.connect().status == shm::io_status::ok);

    std::vector<std::pair<std::string, std::string>> received;
    const std::string topic = "everest/modules/source/impl/main/var/qos-retain";
    subscribe_with_server(fixture.server, subscriber, topic, [&](std::string_view topic_view, std::string_view data) {
        received.push_back({std::string(topic_view), std::string(data)});
    });

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    const auto qos_values = all_qos_values();
    for (std::size_t i = 0; i < qos_values.size(); i++) {
        const auto payload = "qos-retain-policy-payload-" + std::to_string(i);
        publish_with_server(fixture.server, [&]() { mqtt.publish(topic, payload, qos_values[i], true); });
    }
    drive_server_and_client_until(fixture.server, subscriber, [&]() { return received.size() == qos_values.size(); });

    REQUIRE(received.size() == qos_values.size());
    for (std::size_t i = 0; i < qos_values.size(); i++) {
        REQUIRE(received[i].first == topic);
        REQUIRE(received[i].second == "qos-retain-policy-payload-" + std::to_string(i));
    }
}

TEST_CASE("ShmFrameworkTransport retained publishes replay string and JSON payloads to late subscribers") {
    open_server fixture("retained-replay");
    if (!fixture.available) {
        return;
    }

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());

    const std::string string_topic = "everest/modules/source/impl/main/var/raw";
    const std::string string_payload("retained\0string payload", 23);
    const std::string json_topic = "everest/modules/source/impl/main/var/session";
    const nlohmann::json json_payload{{"answer", 42}, {"text", "hello"}};

    publish_with_server(fixture.server,
                        [&]() { mqtt.publish(string_topic, string_payload, Everest::QOS::QOS1, true); });
    REQUIRE(wait_for_condition(
        fixture.server, [&]() { return fixture.server.counter_snapshot().messages_published >= 1U; }, 1s));
    publish_with_server(fixture.server, [&]() { mqtt.publish(json_topic, json_payload, Everest::QOS::QOS2, true); });
    REQUIRE(wait_for_condition(
        fixture.server, [&]() { return fixture.server.counter_snapshot().messages_published >= 2U; }, 1s));

    shm::shm_client string_subscriber(make_client_options(fixture.options, "retained-string-subscriber"));
    REQUIRE(string_subscriber.connect().status == shm::io_status::ok);
    std::string received_string;
    subscribe_with_server(fixture.server, string_subscriber, string_topic,
                          [&](std::string_view, std::string_view payload) { received_string = std::string(payload); });
    REQUIRE(received_string == string_payload);

    shm::shm_client json_subscriber(make_client_options(fixture.options, "retained-json-subscriber"));
    REQUIRE(json_subscriber.connect().status == shm::io_status::ok);
    std::string received_json;
    subscribe_with_server(fixture.server, json_subscriber, json_topic,
                          [&](std::string_view, std::string_view payload) { received_json = std::string(payload); });
    REQUIRE(received_json == json_payload.dump());

    mqtt.clear_retained_topics();
    REQUIRE(wait_for_condition(
        fixture.server, [&]() { return fixture.server.counter_snapshot().messages_published >= 4U; }, 1s));

    shm::shm_client cleared_subscriber(make_client_options(fixture.options, "retained-cleared-subscriber"));
    REQUIRE(cleared_subscriber.connect().status == shm::io_status::ok);
    std::vector<std::string> cleared_payloads;
    subscribe_with_server(fixture.server, cleared_subscriber, string_topic,
                          [&](std::string_view, std::string_view payload) { cleared_payloads.emplace_back(payload); });
    CHECK(cleared_payloads.empty());
}

TEST_CASE("ShmFrameworkTransport retained replay works when subscribe races pending manager dispatch") {
    open_server fixture("retained-race");
    if (!fixture.available) {
        return;
    }

    shm::shm_client publisher(make_client_options(fixture.options, "retained-race-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    const std::string topic = "everest/modules/source/impl/main/var/session";
    shm::publish_options options;
    options.retain = true;
    publish_with_server(fixture.server, [&]() { REQUIRE(publisher.publish(topic, "retained-race", options)); });

    shm::shm_client subscriber(make_client_options(fixture.options, "retained-race-subscriber"));
    REQUIRE(subscriber.connect().status == shm::io_status::ok);
    std::vector<std::string> payloads;
    subscribe_with_server(fixture.server, subscriber, topic,
                          [&](std::string_view, std::string_view payload) { payloads.emplace_back(payload); });

    REQUIRE(payloads == std::vector<std::string>{"retained-race"});
}

TEST_CASE("ShmFrameworkTransport subscribe accepts every QoS value") {
    open_server fixture("qos-subscribe");
    if (!fixture.available) {
        return;
    }

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());

    const std::array<std::string, 3> topics = {"external/source/qos0", "external/source/qos1", "external/source/qos2"};
    const auto qos_values = all_qos_values();
    for (std::size_t i = 0; i < qos_values.size(); i++) {
        adapter_subscribe_with_server(fixture.server, [&]() { mqtt.subscribe(topics[i], qos_values[i]); });
    }
}

TEST_CASE("ShmFrameworkTransport register_handler routes payloads for every QoS value") {
    open_server fixture("qos-register-handler");
    if (!fixture.available) {
        return;
    }

    shm::shm_client publisher(make_client_options(fixture.options, "qos-register-handler-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto main_loop = mqtt.spawn_main_loop_thread();

    std::mutex mutex;
    std::vector<std::pair<std::string, std::string>> received;
    const std::array<std::string, 3> topics = {"external/source/qos0", "external/source/qos1", "external/source/qos2"};
    const auto qos_values = all_qos_values();

    for (std::size_t i = 0; i < qos_values.size(); i++) {
        const auto handler = std::make_shared<TypedHandler>(
            HandlerType::ExternalMQTT, std::make_shared<Handler>([&](const std::string& topic, json data) {
                const std::lock_guard<std::mutex> lock(mutex);
                received.push_back({topic, data.get<std::string>()});
            }));
        adapter_subscribe_with_server(fixture.server,
                                      [&]() { mqtt.register_handler(topics[i], handler, qos_values[i]); });
    }

    for (std::size_t i = 0; i < qos_values.size(); i++) {
        publish_with_server(fixture.server,
                            [&]() { publisher.publish(topics[i], "handler-payload-" + std::to_string(i)); });
    }

    REQUIRE(wait_for_condition(
        fixture.server,
        [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return received.size() == qos_values.size();
        },
        timeout));

    {
        const std::lock_guard<std::mutex> lock(mutex);
        REQUIRE(received.size() == qos_values.size());
        for (std::size_t i = 0; i < qos_values.size(); i++) {
            const auto expected = std::make_pair(topics[i], "handler-payload-" + std::to_string(i));
            REQUIRE(std::find(received.begin(), received.end(), expected) != received.end());
        }
    }

    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport logs and drops SHM sequence anomalies without reconnect") {
    open_server fixture("sequence-anomaly");
    if (!fixture.available) {
        return;
    }

    const std::string topic = "everest/modules/source/impl/main/var/session";
    shm::shm_client publisher(make_client_options(fixture.options, "sequence-anomaly-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto main_loop = mqtt.spawn_main_loop_thread();

    std::mutex mutex;
    std::vector<int> received_values;
    const auto handler = std::make_shared<TypedHandler>(HandlerType::ExternalMQTT,
                                                        std::make_shared<Handler>([&](const std::string&, json data) {
                                                            const std::lock_guard<std::mutex> lock(mutex);
                                                            received_values.push_back(data.at("value").get<int>());
                                                        }));
    adapter_subscribe_with_server(fixture.server, [&]() { mqtt.register_handler(topic, handler, Everest::QOS::QOS2); });

    shm::shared_memory memory(fixture.server.options().shm_name,
                              static_cast<std::size_t>(fixture.server.options().segment_size));
    auto ring = ring_for_topic(memory, topic);

    publish_with_server(fixture.server, [&]() {
        REQUIRE(publisher.publish(topic, json{{"value", 1}}.dump()).status == shm::io_status::ok);
    });
    REQUIRE(wait_for_condition(
        fixture.server,
        [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return received_values == std::vector<int>{1};
        },
        timeout));

    REQUIRE(publisher.publish(topic, json{{"value", 2}}.dump()).status == shm::io_status::ok);
    ring.get_slot_header(1)->sequence = 4;
    fixture.server.sync();
    REQUIRE(wait_for_value([&]() { return mqtt.sequence_anomaly_count() == 1U; }, timeout));

    {
        const std::lock_guard<std::mutex> lock(mutex);
        REQUIRE(received_values == std::vector<int>{1});
    }

    REQUIRE(publisher.publish(topic, json{{"value", 3}}.dump()).status == shm::io_status::ok);
    fixture.server.sync();
    REQUIRE(wait_for_value([&]() { return mqtt.sequence_anomaly_count() == 2U; }, timeout));

    {
        const std::lock_guard<std::mutex> lock(mutex);
        REQUIRE(received_values == std::vector<int>{1});
    }

    REQUIRE(publisher.publish(topic, json{{"value", 4}}.dump()).status == shm::io_status::ok);
    REQUIRE(wait_for_condition(
        fixture.server,
        [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return received_values == std::vector<int>{1, 4};
        },
        timeout));

    REQUIRE(mqtt.sequence_anomaly_count() == 2U);
    REQUIRE(mqtt.unexpected_error_count() == 0U);
    REQUIRE(mqtt.suppressed_shutdown_error_count() == 0U);
    REQUIRE(fixture.server.subscriber_snapshots(topic).size() == 1U);

    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport get accepts every QoS value and returns retained response") {
    open_server fixture("qos-get-response");
    if (!fixture.available) {
        return;
    }

    shm::shm_client publisher(make_client_options(fixture.options, "qos-get-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    for (const auto qos : all_qos_values()) {
        Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
        REQUIRE(mqtt.connect());

        const auto expected = json{{"qos", static_cast<int>(qos)}};
        publish_with_server(fixture.server, [&]() {
            shm::publish_options options;
            options.retain = true;
            publisher.publish("everest/modules/test/response", expected.dump(), options);
        });

        auto result_future =
            std::async(std::launch::async, [&]() { return mqtt.get("everest/modules/test/response", qos); });
        drive_until_ready(fixture.server, result_future);
        REQUIRE(result_future.get() == expected);
        mqtt.disconnect();
    }
}

TEST_CASE("ShmFrameworkTransport repeated retained get resubscribes for retained replay") {
    open_server fixture("get-repeated-retained");
    if (!fixture.available) {
        return;
    }

    shm::shm_client publisher(make_client_options(fixture.options, "get-repeated-retained-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());

    const auto expected = json{{"msg_type", "GetConfigResponse"}, {"data", {{"repeat", true}}}};
    publish_with_server(fixture.server, [&]() {
        shm::publish_options options;
        options.retain = true;
        publisher.publish("everest/modules/test/response", expected.dump(), options);
    });

    for (int i = 0; i < 2; ++i) {
        auto result_future = std::async(
            std::launch::async, [&]() { return mqtt.get("everest/modules/test/response", Everest::QOS::QOS2); });
        drive_until_ready(fixture.server, result_future);
        REQUIRE(result_future.get() == json{{"repeat", true}});
    }

    mqtt.disconnect();
}

TEST_CASE("ShmFrameworkTransport get publishes explicit request and returns response data") {
    open_server fixture("get-explicit-request");
    if (!fixture.available) {
        return;
    }

    shm::shm_client responder(make_client_options(fixture.options, "get-explicit-responder"));
    REQUIRE(responder.connect().status == shm::io_status::ok);

    std::promise<json> request_payload;
    auto request_future = request_payload.get_future();
    subscribe_with_server(
        fixture.server, responder, "everest/config/request",
        [&](std::string_view, std::string_view payload) { request_payload.set_value(json::parse(payload)); });

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());

    Everest::MQTTRequest request;
    request.response_topic = "everest/modules/test/response";
    request.request_topic = "everest/config/request";
    request.request_data = json{{"request", "config"}}.dump();
    request.timeout = 500ms;

    auto result_future = std::async(std::launch::async, [&]() { return mqtt.get(request); });
    drive_server_and_client_until(fixture.server, responder,
                                  [&]() { return request_future.wait_for(0ms) == std::future_status::ready; });

    const auto response_data = json{{"status", "ok"}, {"value", 42}};
    const MqttMessagePayload response{MqttMessageType::GetConfigResponse, response_data};
    publish_with_server(fixture.server,
                        [&]() { responder.publish("everest/modules/test/response", json(response).dump()); });
    drive_until_ready(fixture.server, result_future);

    REQUIRE(result_future.get() == json{{"status", "ok"}, {"value", 42}});
    REQUIRE(request_future.get() == json{{"msg_type", "GetConfig"}, {"data", {{"request", "config"}}}});
}

TEST_CASE("ShmFrameworkTransport get can be called from a subscribed handler") {
    open_server fixture("get-from-handler");
    if (!fixture.available) {
        return;
    }

    shm::shm_client responder(make_client_options(fixture.options, "get-from-handler-responder"));
    REQUIRE(responder.connect().status == shm::io_status::ok);

    std::atomic_bool request_seen{false};
    subscribe_with_server(fixture.server, responder, "everest/config/request",
                          [&](std::string_view, std::string_view) { request_seen = true; });

    shm::shm_client publisher(make_client_options(fixture.options, "get-from-handler-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto main_loop = mqtt.spawn_main_loop_thread();

    std::promise<json> handler_result;
    auto handler_future = handler_result.get_future();
    const auto handler = std::make_shared<TypedHandler>(
        HandlerType::ExternalMQTT, std::make_shared<Handler>([&](const std::string&, json) {
            try {
                Everest::MQTTRequest request;
                request.response_topic = "everest/modules/test/response";
                request.request_topic = "everest/config/request";
                request.request_data = json{{"request", "from-handler"}}.dump();
                request.timeout = 500ms;
                handler_result.set_value(mqtt.get(request));
            } catch (...) {
                handler_result.set_exception(std::current_exception());
            }
        }));

    adapter_subscribe_with_server(fixture.server,
                                  [&]() { mqtt.register_handler("external/source/a", handler, Everest::QOS::QOS1); });

    publish_with_server(fixture.server, [&]() { publisher.publish("external/source/a", "trigger"); });
    drive_server_and_client_until(fixture.server, responder, [&]() { return request_seen.load(); });

    const MqttMessagePayload response{MqttMessageType::GetConfigResponse, json{{"handler", "ok"}}};
    publish_with_server(fixture.server,
                        [&]() { responder.publish("everest/modules/test/response", json(response).dump()); });

    REQUIRE(wait_for_condition(
        fixture.server, [&]() { return handler_future.wait_for(0ms) == std::future_status::ready; }, timeout));
    REQUIRE(handler_future.get() == json{{"handler", "ok"}});

    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport get tolerates response payloads with non-string msg_type") {
    open_server fixture("get-non-string-msg-type");
    if (!fixture.available) {
        return;
    }

    shm::shm_client publisher(make_client_options(fixture.options, "get-non-string-msg-type-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());

    const auto response = json{{"msg_type", false}, {"data", {{"ignored", true}}}};
    publish_with_server(fixture.server, [&]() {
        shm::publish_options options;
        options.retain = true;
        publisher.publish("everest/modules/test/response", response.dump(), options);
    });

    auto result_future =
        std::async(std::launch::async, [&]() { return mqtt.get("everest/modules/test/response", Everest::QOS::QOS2); });
    drive_until_ready(fixture.server, result_future);

    REQUIRE(result_future.get() == response);
}

TEST_CASE("ShmFrameworkTransport get tolerates GetConfigResponse payloads without data") {
    open_server fixture("get-missing-data");
    if (!fixture.available) {
        return;
    }

    shm::shm_client publisher(make_client_options(fixture.options, "get-missing-data-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());

    const auto response = json{{"msg_type", "GetConfigResponse"}};
    publish_with_server(fixture.server, [&]() {
        shm::publish_options options;
        options.retain = true;
        publisher.publish("everest/modules/test/response", response.dump(), options);
    });

    auto result_future =
        std::async(std::launch::async, [&]() { return mqtt.get("everest/modules/test/response", Everest::QOS::QOS2); });
    drive_until_ready(fixture.server, result_future);

    REQUIRE(result_future.get() == response);
}

TEST_CASE("ShmFrameworkTransport get times out with clear EverestTimeoutError") {
    open_server fixture("get-timeout");
    if (!fixture.available) {
        return;
    }

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());

    Everest::MQTTRequest request;
    request.response_topic = "everest/modules/test/response";
    request.timeout = 20ms;

    auto result_future = std::async(std::launch::async, [&]() {
        try {
            (void)mqtt.get(request);
        } catch (const std::exception& e) {
            return std::string(e.what());
        }
        return std::string();
    });
    drive_until_ready(fixture.server, result_future);
    REQUIRE_THAT(result_future.get(), Catch::Matchers::ContainsSubstring("Timeout while waiting for SHM result"));
}

TEST_CASE("ShmFrameworkTransport get retries after timeout") {
    open_server fixture("get-retry");
    if (!fixture.available) {
        return;
    }

    shm::shm_client responder(make_client_options(fixture.options, "get-retry-responder"));
    REQUIRE(responder.connect().status == shm::io_status::ok);

    std::atomic_int attempts{0};
    subscribe_with_server(fixture.server, responder, "everest/config/request",
                          [&](std::string_view, std::string_view) { attempts.fetch_add(1); });

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());

    Everest::MQTTRequest request;
    request.response_topic = "everest/modules/retry/response";
    request.request_topic = "everest/config/request";
    request.request_data = json{{"request", "retry"}}.dump();
    request.timeout = 100ms;

    auto result_future = std::async(std::launch::async, [&]() { return mqtt.get(request, 1); });
    drive_server_and_client_until(fixture.server, responder, [&]() { return attempts.load() >= 2; });

    const MqttMessagePayload response{MqttMessageType::GetConfigResponse, json{{"retry", "ok"}}};
    publish_with_server(fixture.server,
                        [&]() { responder.publish("everest/modules/retry/response", json(response).dump()); });
    drive_until_ready(fixture.server, result_future);

    REQUIRE(result_future.get() == json{{"retry", "ok"}});
    REQUIRE(attempts.load() == 2);
}

TEST_CASE("ShmFrameworkTransport drains late response before next explicit get on same response topic") {
    open_server fixture("get-drains-late-response");
    if (!fixture.available) {
        return;
    }

    shm::shm_client responder(make_client_options(fixture.options, "get-drains-late-response-responder"));
    REQUIRE(responder.connect().status == shm::io_status::ok);

    std::atomic_int attempts{0};
    subscribe_with_server(fixture.server, responder, "everest/config/request",
                          [&](std::string_view, std::string_view) { attempts.fetch_add(1); });

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());

    Everest::MQTTRequest request;
    request.response_topic = "everest/modules/retry/response";
    request.request_topic = "everest/config/request";
    request.request_data = json{{"request", "drain-late"}}.dump();
    request.timeout = 20ms;

    auto timed_out = std::async(std::launch::async, [&]() {
        try {
            (void)mqtt.get(request);
        } catch (const Everest::EverestTimeoutError&) {
            return true;
        }
        return false;
    });
    drive_server_and_client_until(fixture.server, responder, [&]() { return attempts.load() >= 1; });
    drive_until_ready(fixture.server, timed_out);
    REQUIRE(timed_out.get());

    const MqttMessagePayload late_response{MqttMessageType::GetConfigResponse, json{{"response", "late"}}};
    publish_with_server(fixture.server,
                        [&]() { responder.publish("everest/modules/retry/response", json(late_response).dump()); });

    request.timeout = 500ms;
    auto result_future = std::async(std::launch::async, [&]() { return mqtt.get(request); });
    drive_server_and_client_until(fixture.server, responder, [&]() { return attempts.load() >= 2; });

    const MqttMessagePayload current_response{MqttMessageType::GetConfigResponse, json{{"response", "current"}}};
    publish_with_server(fixture.server,
                        [&]() { responder.publish("everest/modules/retry/response", json(current_response).dump()); });
    drive_until_ready(fixture.server, result_future);

    REQUIRE(result_future.get() == json{{"response", "current"}});
    REQUIRE(attempts.load() == 2);
}

TEST_CASE("ShmFrameworkTransport concurrent get calls keep independent response topics isolated") {
    open_server fixture("get-independent-topics");
    if (!fixture.available) {
        return;
    }

    shm::shm_client responder(make_client_options(fixture.options, "get-independent-responder"));
    REQUIRE(responder.connect().status == shm::io_status::ok);

    std::mutex requests_mutex;
    std::vector<std::string> requests;
    subscribe_with_server(fixture.server, responder, "everest/config/request",
                          [&](std::string_view, std::string_view payload) {
                              const auto name = json::parse(payload).at("data").at("name").get<std::string>();
                              const std::lock_guard<std::mutex> lock(requests_mutex);
                              requests.push_back(name);
                          });

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto main_loop = mqtt.spawn_main_loop_thread();

    Everest::MQTTRequest request_a;
    request_a.response_topic = "everest/modules/a/response";
    request_a.request_topic = "everest/config/request";
    request_a.request_data = json{{"name", "a"}}.dump();
    request_a.timeout = 500ms;

    Everest::MQTTRequest request_b;
    request_b.response_topic = "everest/modules/b/response";
    request_b.request_topic = "everest/config/request";
    request_b.request_data = json{{"name", "b"}}.dump();
    request_b.timeout = 500ms;

    auto result_a = std::async(std::launch::async, [&]() { return mqtt.get(request_a); });
    auto result_b = std::async(std::launch::async, [&]() { return mqtt.get(request_b); });

    drive_server_and_client_until(fixture.server, responder, [&]() {
        const std::lock_guard<std::mutex> lock(requests_mutex);
        return requests.size() == 2;
    });

    const MqttMessagePayload response_a{MqttMessageType::GetConfigResponse, json{{"name", "a"}}};
    const MqttMessagePayload response_b{MqttMessageType::GetConfigResponse, json{{"name", "b"}}};
    publish_with_server(fixture.server,
                        [&]() { responder.publish("everest/modules/b/response", json(response_b).dump()); });
    publish_with_server(fixture.server,
                        [&]() { responder.publish("everest/modules/a/response", json(response_a).dump()); });
    drive_until_ready(fixture.server, result_a);
    drive_until_ready(fixture.server, result_b);

    REQUIRE(result_a.get() == json{{"name", "a"}});
    REQUIRE(result_b.get() == json{{"name", "b"}});

    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport subscribe delivers SHM payloads through MessageHandler") {
    open_server fixture("subscribe-delivery");
    if (!fixture.available) {
        return;
    }

    shm::shm_client publisher(make_client_options(fixture.options, "subscribe-delivery-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto main_loop = mqtt.spawn_main_loop_thread();

    std::promise<std::pair<std::string, std::string>> received;
    auto received_future = received.get_future();
    const auto handler = std::make_shared<TypedHandler>(
        HandlerType::ExternalMQTT, std::make_shared<Handler>([&](const std::string& topic, json data) {
            received.set_value({topic, data.get<std::string>()});
        }));

    const std::string topic = "external/source/a";
    adapter_subscribe_with_server(fixture.server, [&]() { mqtt.register_handler(topic, handler, Everest::QOS::QOS1); });

    publish_with_server(fixture.server, [&]() { publisher.publish(topic, "adapter-payload"); });
    REQUIRE(wait_for_condition(
        fixture.server, [&]() { return received_future.wait_for(0ms) == std::future_status::ready; }, timeout));

    const auto [received_topic, received_payload] = received_future.get();
    REQUIRE(received_topic == topic);
    REQUIRE(received_payload == "adapter-payload");

    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport dispatches multiple handlers without duplicate SHM subscriptions") {
    open_server fixture("multiple-handlers");
    if (!fixture.available) {
        return;
    }

    shm::shm_client publisher(make_client_options(fixture.options, "multiple-handlers-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto main_loop = mqtt.spawn_main_loop_thread();

    std::mutex mutex;
    std::vector<std::pair<std::string, std::string>> received;
    auto make_handler = [&](std::string name) {
        return std::make_shared<TypedHandler>(
            HandlerType::ExternalMQTT,
            std::make_shared<Handler>([&, name = std::move(name)](const std::string& topic, json data) {
                const std::lock_guard<std::mutex> lock(mutex);
                received.push_back({name + ":" + topic, data.get<std::string>()});
            }));
    };

    const std::string topic = "external/source/a";
    adapter_subscribe_with_server(fixture.server,
                                  [&]() { mqtt.register_handler(topic, make_handler("first"), Everest::QOS::QOS0); });
    adapter_subscribe_with_server(fixture.server,
                                  [&]() { mqtt.register_handler(topic, make_handler("second"), Everest::QOS::QOS0); });

    publish_with_server(fixture.server, [&]() { publisher.publish(topic, "single-publish"); });
    REQUIRE(wait_for_condition(
        fixture.server,
        [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return received.size() >= 2;
        },
        timeout));

    REQUIRE_FALSE(wait_for_condition(
        fixture.server,
        [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return received.size() > 2;
        },
        100ms));

    {
        const std::lock_guard<std::mutex> lock(mutex);
        REQUIRE(received.size() == 2);
        REQUIRE(std::find(received.begin(), received.end(),
                          std::make_pair(std::string("first:") + topic, std::string("single-publish"))) !=
                received.end());
        REQUIRE(std::find(received.begin(), received.end(),
                          std::make_pair(std::string("second:") + topic, std::string("single-publish"))) !=
                received.end());
    }

    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport unregister_handler removes only the selected handler") {
    open_server fixture("unregister-handler");
    if (!fixture.available) {
        return;
    }

    shm::shm_client publisher(make_client_options(fixture.options, "unregister-handler-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto main_loop = mqtt.spawn_main_loop_thread();

    std::mutex mutex;
    std::vector<std::pair<std::string, std::string>> received;
    auto make_handler = [&](std::string name) {
        return std::make_shared<TypedHandler>(
            HandlerType::ExternalMQTT,
            std::make_shared<Handler>([&, name = std::move(name)](const std::string&, json data) {
                const std::lock_guard<std::mutex> lock(mutex);
                received.push_back({name, data.get<std::string>()});
            }));
    };

    const std::string topic = "external/source/a";
    auto first_handler = make_handler("first");
    auto second_handler = make_handler("second");

    adapter_subscribe_with_server(fixture.server,
                                  [&]() { mqtt.register_handler(topic, first_handler, Everest::QOS::QOS0); });
    adapter_subscribe_with_server(fixture.server,
                                  [&]() { mqtt.register_handler(topic, second_handler, Everest::QOS::QOS0); });

    publish_with_server(fixture.server, [&]() { publisher.publish(topic, "before-unregister"); });
    REQUIRE(wait_for_condition(
        fixture.server,
        [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return received.size() == 2;
        },
        timeout));

    adapter_subscribe_with_server(fixture.server, [&]() { mqtt.unregister_handler(topic, first_handler); });
    publish_with_server(fixture.server, [&]() { publisher.publish(topic, "after-unregister"); });
    REQUIRE(wait_for_condition(
        fixture.server,
        [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return received.size() == 3;
        },
        timeout));

    {
        const std::lock_guard<std::mutex> lock(mutex);
        REQUIRE(received == std::vector<std::pair<std::string, std::string>>{
                                {"first", "before-unregister"},
                                {"second", "before-unregister"},
                                {"second", "after-unregister"},
                            });
    }

    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport expands wildcard handlers to registered exact SHM topics") {
    open_server fixture("wildcard-expansion");
    if (!fixture.available) {
        return;
    }

    shm::shm_client publisher(make_client_options(fixture.options, "wildcard-expansion-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto main_loop = mqtt.spawn_main_loop_thread();

    std::mutex mutex;
    std::vector<std::pair<std::string, std::string>> received;
    auto handler = std::make_shared<TypedHandler>(HandlerType::ExternalMQTT,
                                                  std::make_shared<Handler>([&](const std::string& topic, json data) {
                                                      const std::lock_guard<std::mutex> lock(mutex);
                                                      received.push_back({topic, data.get<std::string>()});
                                                  }));

    adapter_subscribe_with_server(fixture.server,
                                  [&]() { mqtt.register_handler("external/source/#", handler, Everest::QOS::QOS0); });

    REQUIRE(fixture.server.subscriber_snapshots("external/source/a").size() == 1);
    REQUIRE(fixture.server.subscriber_snapshots("external/source/b").size() == 1);
    publish_with_server(fixture.server, [&]() { publisher.publish("external/source/a", "payload-a"); });
    publish_with_server(fixture.server, [&]() { publisher.publish("external/source/b", "payload-b"); });
    REQUIRE(wait_for_condition(
        fixture.server,
        [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return received.size() == 2;
        },
        timeout));

    const std::lock_guard<std::mutex> lock(mutex);
    REQUIRE(received == std::vector<std::pair<std::string, std::string>>{
                            {"external/source/a", "payload-a"},
                            {"external/source/b", "payload-b"},
                        });

    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport rejects invalid wildcard filters before SHM subscribe") {
    open_server fixture("wildcard-invalid");
    if (!fixture.available) {
        return;
    }

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto handler =
        std::make_shared<TypedHandler>(HandlerType::ExternalMQTT, std::make_shared<Handler>([](auto, auto) {}));

    auto future = std::async(std::launch::async,
                             [&]() { mqtt.register_handler("external/source/#/bad", handler, Everest::QOS::QOS0); });
    drive_until_ready(fixture.server, future);
    REQUIRE_THROWS_WITH(future.get(), Catch::Matchers::ContainsSubstring("Invalid SHM topic filter"));
    REQUIRE(fixture.server.counter_snapshot().subscriber_joins == 0);
}

TEST_CASE("ShmFrameworkTransport throws clearly when wildcard filters match no registered exact SHM topics") {
    open_server fixture("wildcard-no-match");
    if (!fixture.available) {
        return;
    }

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto handler =
        std::make_shared<TypedHandler>(HandlerType::ExternalMQTT, std::make_shared<Handler>([](auto, auto) {}));

    auto future =
        std::async(std::launch::async, [&]() { mqtt.register_handler("unregistered/#", handler, Everest::QOS::QOS0); });
    drive_until_ready(fixture.server, future);
    REQUIRE_THROWS_WITH(future.get(), Catch::Matchers::ContainsSubstring("matched no registered exact topics"));
    REQUIRE(fixture.server.counter_snapshot().subscriber_joins == 0);
}

TEST_CASE("ShmFrameworkTransport wildcard unregister keeps overlapping concrete subscriptions") {
    open_server fixture("wildcard-overlap");
    if (!fixture.available) {
        return;
    }

    shm::shm_client publisher(make_client_options(fixture.options, "wildcard-overlap-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto main_loop = mqtt.spawn_main_loop_thread();

    std::mutex mutex;
    std::vector<std::pair<std::string, std::string>> received;
    auto make_handler = [&](std::string name) {
        return std::make_shared<TypedHandler>(
            HandlerType::ExternalMQTT,
            std::make_shared<Handler>([&, name = std::move(name)](const std::string& topic, json data) {
                const std::lock_guard<std::mutex> lock(mutex);
                received.push_back({name + ":" + topic, data.get<std::string>()});
            }));
    };

    auto exact_handler = make_handler("exact");
    auto wildcard_handler = make_handler("wildcard");
    adapter_subscribe_with_server(
        fixture.server, [&]() { mqtt.register_handler("external/source/a", exact_handler, Everest::QOS::QOS0); });
    adapter_subscribe_with_server(
        fixture.server, [&]() { mqtt.register_handler("external/source/#", wildcard_handler, Everest::QOS::QOS0); });

    REQUIRE(fixture.server.subscriber_snapshots("external/source/a").size() == 1);
    REQUIRE(fixture.server.subscriber_snapshots("external/source/b").size() == 1);

    adapter_subscribe_with_server(fixture.server,
                                  [&]() { mqtt.unregister_handler("external/source/#", wildcard_handler); });

    REQUIRE(fixture.server.subscriber_snapshots("external/source/a").size() == 1);
    REQUIRE(fixture.server.subscriber_snapshots("external/source/b").empty());

    publish_with_server(fixture.server, [&]() { publisher.publish("external/source/a", "after-wildcard"); });
    publish_with_server(fixture.server, [&]() { publisher.publish("external/source/b", "after-wildcard"); });
    REQUIRE(wait_for_condition(
        fixture.server,
        [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return received.size() == 1;
        },
        timeout));
    REQUIRE_FALSE(wait_for_condition(
        fixture.server,
        [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return received.size() > 1;
        },
        100ms));

    {
        const std::lock_guard<std::mutex> lock(mutex);
        REQUIRE(received == std::vector<std::pair<std::string, std::string>>{
                                {"exact:external/source/a", "after-wildcard"},
                            });
    }

    adapter_subscribe_with_server(fixture.server,
                                  [&]() { mqtt.unregister_handler("external/source/a", exact_handler); });
    REQUIRE(fixture.server.subscriber_snapshots("external/source/a").empty());

    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport wildcard handlers receive retained replay from matching exact SHM topics") {
    open_server fixture("wildcard-retained");
    if (!fixture.available) {
        return;
    }

    Everest::ShmFrameworkTransport publisher(make_mqtt_settings(fixture.options));
    REQUIRE(publisher.connect());
    publish_with_server(fixture.server, [&]() {
        publisher.publish("external/source/a", std::string("retained-a"), Everest::QOS::QOS1, true);
    });
    publish_with_server(fixture.server, [&]() {
        publisher.publish("external/source/b", std::string("retained-b"), Everest::QOS::QOS1, true);
    });

    Everest::ShmFrameworkTransport subscriber(make_mqtt_settings(fixture.options));
    REQUIRE(subscriber.connect());
    auto main_loop = subscriber.spawn_main_loop_thread();

    std::mutex mutex;
    std::vector<std::pair<std::string, std::string>> received;
    auto handler = std::make_shared<TypedHandler>(HandlerType::ExternalMQTT,
                                                  std::make_shared<Handler>([&](const std::string& topic, json data) {
                                                      const std::lock_guard<std::mutex> lock(mutex);
                                                      received.push_back({topic, data.get<std::string>()});
                                                  }));

    adapter_subscribe_with_server(
        fixture.server, [&]() { subscriber.register_handler("external/source/#", handler, Everest::QOS::QOS0); });
    REQUIRE(wait_for_condition(
        fixture.server,
        [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return received.size() == 2;
        },
        timeout));

    std::sort(received.begin(), received.end());
    REQUIRE(received == std::vector<std::pair<std::string, std::string>>{
                            {"external/source/a", "retained-a"},
                            {"external/source/b", "retained-b"},
                        });

    subscriber.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport main loop future is stable before spawn and repeated spawn") {
    open_server fixture("main-loop-future");
    if (!fixture.available) {
        return;
    }

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    auto before_spawn = mqtt.get_main_loop_future();
    REQUIRE(before_spawn.valid());
    REQUIRE(before_spawn.wait_for(0ms) == std::future_status::ready);

    REQUIRE(mqtt.connect());
    auto first = mqtt.spawn_main_loop_thread();
    auto second = mqtt.spawn_main_loop_thread();
    auto current = mqtt.get_main_loop_future();

    REQUIRE(first.valid());
    REQUIRE(second.valid());
    REQUIRE(current.valid());
    REQUIRE(first.wait_for(0ms) != std::future_status::ready);
    REQUIRE(second.wait_for(0ms) != std::future_status::ready);
    REQUIRE(current.wait_for(0ms) != std::future_status::ready);

    mqtt.disconnect();
    REQUIRE(first.wait_for(timeout) == std::future_status::ready);
    REQUIRE(second.wait_for(timeout) == std::future_status::ready);
    REQUIRE(current.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport background loop delivers without manual adapter sync") {
    open_server fixture("background-loop-delivery");
    if (!fixture.available) {
        return;
    }

    shm::shm_client publisher(make_client_options(fixture.options, "background-loop-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto main_loop = mqtt.spawn_main_loop_thread();

    std::promise<std::string> received;
    auto received_future = received.get_future();
    const auto handler = std::make_shared<TypedHandler>(
        HandlerType::ExternalMQTT,
        std::make_shared<Handler>([&](const std::string&, json data) { received.set_value(data.get<std::string>()); }));

    const std::string topic = "external/source/b";
    adapter_subscribe_with_server(fixture.server, [&]() { mqtt.register_handler(topic, handler, Everest::QOS::QOS0); });

    publish_with_server(fixture.server, [&]() { publisher.publish(topic, "background-loop-payload"); });
    REQUIRE(wait_for_condition(
        fixture.server, [&]() { return received_future.wait_for(0ms) == std::future_status::ready; }, timeout));
    REQUIRE(received_future.get() == "background-loop-payload");

    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport shutdown completes with active subscriptions") {
    open_server fixture("shutdown-active-subscriptions");
    if (!fixture.available) {
        return;
    }

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto main_loop = mqtt.spawn_main_loop_thread();

    adapter_subscribe_with_server(fixture.server, [&]() { mqtt.subscribe("external/source/a"); });
    adapter_subscribe_with_server(fixture.server, [&]() { mqtt.subscribe("external/source/b"); });

    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport unsubscribe stops delivery and tolerates unknown topics") {
    open_server fixture("unsubscribe");
    if (!fixture.available) {
        return;
    }

    shm::shm_client publisher(make_client_options(fixture.options, "unsubscribe-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE_NOTHROW(mqtt.unsubscribe("external/source/a"));
    REQUIRE(mqtt.connect());
    auto main_loop = mqtt.spawn_main_loop_thread();

    std::mutex mutex;
    std::vector<std::string> received;
    const auto handler = std::make_shared<TypedHandler>(HandlerType::ExternalMQTT,
                                                        std::make_shared<Handler>([&](const std::string&, json data) {
                                                            const std::lock_guard<std::mutex> lock(mutex);
                                                            received.push_back(data.get<std::string>());
                                                        }));

    const std::string topic = "external/source/a";
    adapter_subscribe_with_server(fixture.server, [&]() { mqtt.register_handler(topic, handler, Everest::QOS::QOS0); });
    REQUIRE_NOTHROW(mqtt.subscribe(topic, Everest::QOS::QOS2));

    publish_with_server(fixture.server, [&]() { publisher.publish(topic, "first"); });
    REQUIRE(wait_for_condition(
        fixture.server,
        [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return received.size() == 1;
        },
        timeout));

    adapter_subscribe_with_server(fixture.server, [&]() { mqtt.unsubscribe(topic); });
    REQUIRE_NOTHROW(mqtt.unsubscribe(topic));

    publish_with_server(fixture.server, [&]() { publisher.publish(topic, "second"); });
    REQUIRE_FALSE(wait_for_condition(
        fixture.server,
        [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return received.size() > 1;
        },
        100ms));

    {
        const std::lock_guard<std::mutex> lock(mutex);
        REQUIRE(received == std::vector<std::string>{"first"});
    }

    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport keeps multiple subscribed topics isolated") {
    open_server fixture("multi-topic");
    if (!fixture.available) {
        return;
    }

    shm::shm_client publisher(make_client_options(fixture.options, "multi-topic-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto main_loop = mqtt.spawn_main_loop_thread();

    std::mutex mutex;
    std::vector<std::pair<std::string, std::string>> received;
    auto make_handler = [&]() {
        return std::make_shared<TypedHandler>(HandlerType::ExternalMQTT,
                                              std::make_shared<Handler>([&](const std::string& topic, json data) {
                                                  const std::lock_guard<std::mutex> lock(mutex);
                                                  received.push_back({topic, data.get<std::string>()});
                                              }));
    };

    adapter_subscribe_with_server(
        fixture.server, [&]() { mqtt.register_handler("external/source/a", make_handler(), Everest::QOS::QOS0); });
    adapter_subscribe_with_server(
        fixture.server, [&]() { mqtt.register_handler("external/source/b", make_handler(), Everest::QOS::QOS0); });

    publish_with_server(fixture.server, [&]() { publisher.publish("external/source/a", "payload-a"); });
    publish_with_server(fixture.server, [&]() { publisher.publish("external/source/b", "payload-b"); });
    REQUIRE(wait_for_condition(
        fixture.server,
        [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return received.size() == 2;
        },
        timeout));

    const std::lock_guard<std::mutex> lock(mutex);
    REQUIRE(received[0] == std::make_pair(std::string("external/source/a"), std::string("payload-a")));
    REQUIRE(received[1] == std::make_pair(std::string("external/source/b"), std::string("payload-b")));

    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
}

TEST_CASE("ShmFrameworkTransport subscribe lifecycle failures are predictable") {
    const auto server_options = make_server_options("subscribe-lifecycle");
    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(server_options));

    REQUIRE_THROWS_WITH(mqtt.subscribe("external/source/a", Everest::QOS::QOS1),
                        Catch::Matchers::ContainsSubstring("not_open"));
    REQUIRE_NOTHROW(mqtt.unsubscribe("external/source/a"));
}

TEST_CASE("ShmFrameworkTransport disconnect clears active subscription bookkeeping") {
    open_server fixture("disconnect-active-subscription");
    if (!fixture.available) {
        return;
    }

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    auto main_loop = mqtt.spawn_main_loop_thread();

    adapter_subscribe_with_server(fixture.server, [&]() { mqtt.subscribe("external/source/a"); });
    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);

    REQUIRE_NOTHROW(mqtt.unsubscribe("external/source/a"));
    REQUIRE_THROWS_WITH(mqtt.subscribe("external/source/a"), Catch::Matchers::ContainsSubstring("not_open"));
}

TEST_CASE("ShmFrameworkTransport controlled shutdown suppresses control-owner loss with active subscriptions") {
    open_server fixture("controlled-shutdown-active-subscriptions");
    if (!fixture.available) {
        return;
    }

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    REQUIRE_FALSE(mqtt.is_controlled_shutdown());
    REQUIRE(mqtt.suppressed_shutdown_error_count() == 0);
    REQUIRE(mqtt.unexpected_error_count() == 0);
    auto main_loop = mqtt.spawn_main_loop_thread();

    adapter_subscribe_with_server(fixture.server, [&]() { mqtt.subscribe("external/source/a"); });
    adapter_subscribe_with_server(fixture.server, [&]() { mqtt.subscribe("external/source/b"); });

    REQUIRE_FALSE(mqtt.begin_controlled_shutdown());
    REQUIRE(mqtt.is_controlled_shutdown());
    REQUIRE(fixture.server.close().status == shm::io_status::ok);

    REQUIRE(wait_for_value([&]() { return mqtt.suppressed_shutdown_error_count() > 0; }, timeout));
    REQUIRE(mqtt.unexpected_error_count() == 0);
    const auto suppressed_before_publish = mqtt.suppressed_shutdown_error_count();
    REQUIRE_NOTHROW(mqtt.publish("external/source/a", std::string("after-close")));
    REQUIRE(mqtt.suppressed_shutdown_error_count() > suppressed_before_publish);

    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
    REQUIRE(mqtt.unexpected_error_count() == 0);
}

TEST_CASE("ShmFrameworkTransport reports unexpected control-owner loss while running") {
    open_server fixture("unexpected-control-owner-loss");
    if (!fixture.available) {
        return;
    }

    Everest::ShmFrameworkTransport mqtt(make_mqtt_settings(fixture.options));
    REQUIRE(mqtt.connect());
    REQUIRE_FALSE(mqtt.is_controlled_shutdown());
    auto main_loop = mqtt.spawn_main_loop_thread();

    adapter_subscribe_with_server(fixture.server, [&]() { mqtt.subscribe("external/source/a"); });
    REQUIRE(fixture.server.close().status == shm::io_status::ok);

    REQUIRE(wait_for_value([&]() { return mqtt.unexpected_error_count() > 0; }, timeout));
    REQUIRE(mqtt.suppressed_shutdown_error_count() == 0);
    REQUIRE_THROWS_WITH(mqtt.publish("external/source/a", std::string("after-close")),
                        Catch::Matchers::ContainsSubstring("not_open"));

    const auto unexpected_before_disconnect = mqtt.unexpected_error_count();
    mqtt.disconnect();
    REQUIRE(main_loop.wait_for(timeout) == std::future_status::ready);
    REQUIRE(mqtt.unexpected_error_count() == unexpected_before_disconnect);
}
