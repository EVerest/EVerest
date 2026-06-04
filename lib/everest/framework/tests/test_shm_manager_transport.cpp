// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <utils/shm_manager_transport.hpp>
#include <utils/yaml_loader.hpp>

#include <framework/ModuleAdapter.hpp>
#include <framework/everest.hpp>
#include <tests/mock_framework_transport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <utility>

#include <everest/io/shm/shared_memory.hpp>
#include <everest/io/shm/shm_client.hpp>
#include <everest/io/shm/structures.hpp>
#include <tests/helpers.hpp>

using Catch::Matchers::ContainsSubstring;
using namespace std::chrono_literals;

namespace {

namespace shm = everest::lib::io::shm;

bool has_topic(const everest::lib::io::shm::server_options& options, const std::string& topic) {
    return std::any_of(options.topics.begin(), options.topics.end(),
                       [&topic](const auto& candidate) { return candidate.name == topic; });
}

const everest::lib::io::shm::topic_definition* find_topic(const everest::lib::io::shm::server_options& options,
                                                          const std::string& topic) {
    const auto found = std::find_if(options.topics.begin(), options.topics.end(),
                                    [&topic](const auto& candidate) { return candidate.name == topic; });
    return found == options.topics.end() ? nullptr : &*found;
}

Everest::ManagerConfig make_two_module_config(Everest::MQTTSettings& mqtt_settings) {
    const auto bin_dir = Everest::tests::get_bin_dir().string() + "/";
    auto manager_settings =
        Everest::ManagerSettings(bin_dir + "two_module_test/", bin_dir + "two_module_test/config.yaml");
    manager_settings.mqtt_settings = mqtt_settings;
    return Everest::ManagerConfig(manager_settings);
}

Everest::ManagerConfig make_two_module_config_with_external_mqtt(Everest::MQTTSettings& mqtt_settings) {
    static std::uint64_t fixture_id = 0;
    const auto source_dir = Everest::tests::get_bin_dir() / "two_module_test";
    const auto fixture_dir = fs::temp_directory_path() / ("everest-shm-external-mqtt-" + std::to_string(::getpid()) +
                                                          "-" + std::to_string(fixture_id++));
    fs::remove_all(fixture_dir);
    fs::copy(source_dir, fixture_dir, fs::copy_options::recursive);

    std::ofstream manifest(fixture_dir / "modules/TESTModuleB/manifest.yaml", std::ios::app);
    manifest << "\nenable_external_mqtt: true\n";
    manifest.close();

    auto manager_settings =
        Everest::ManagerSettings(fixture_dir.string() + "/", (fixture_dir / "config.yaml").string());
    manager_settings.mqtt_settings = mqtt_settings;
    return Everest::ManagerConfig(manager_settings);
}

Everest::ManagerConfig make_two_module_config_with_telemetry(Everest::MQTTSettings& mqtt_settings) {
    static std::uint64_t fixture_id = 0;
    const auto source_dir = Everest::tests::get_bin_dir() / "two_module_test";
    const auto fixture_dir = fs::temp_directory_path() / ("everest-shm-telemetry-" + std::to_string(::getpid()) + "-" +
                                                          std::to_string(fixture_id++));
    fs::remove_all(fixture_dir);
    fs::copy(source_dir, fixture_dir, fs::copy_options::recursive);

    std::ofstream manifest(fixture_dir / "modules/TESTModuleB/manifest.yaml", std::ios::app);
    manifest << "\nenable_telemetry: true\n";
    manifest.close();

    auto config_json = Everest::load_yaml(fixture_dir / "config.yaml");
    config_json["active_modules"]["module_b"]["telemetry"] = {{"id", 7}};
    std::ofstream config(fixture_dir / "config.yaml");
    config << config_json.dump(2);
    config.close();

    auto manager_settings =
        Everest::ManagerSettings(fixture_dir.string() + "/", (fixture_dir / "config.yaml").string());
    manager_settings.mqtt_settings = mqtt_settings;
    manager_settings.runtime_settings.telemetry_enabled = true;
    manager_settings.runtime_settings.telemetry_prefix = "telemetry/";
    return Everest::ManagerConfig(manager_settings);
}

nlohmann::json make_serialized_module_config(Everest::ManagerConfig& manager_config, const std::string& module_id) {
    auto serialized = Everest::get_serialized_module_config(module_id, manager_config.get_module_configurations());
    serialized["manifests"] = manager_config.get_manifests();
    serialized["interface_definitions"] = manager_config.get_interface_definitions();
    serialized["types"] = manager_config.get_types();
    serialized["module_names"] = manager_config.get_module_names();
    return serialized;
}

shm::server_options make_loop_test_options(std::string suffix) {
    shm::server_options options;
    options.shm_name = "/everest-framework-manager-loop-" + suffix;
    options.control_socket_name = "/tmp/everest-framework-manager-loop-" + std::to_string(::getpid()) + "-" + suffix;
    options.control_socket_abstract_namespace = false;
    options.unlink_shm_on_close = true;
    options.unlink_control_socket_on_close = true;
    options.topics.push_back({"everest/loop/topic", 4, 512});
    return options;
}

shm::client_options make_client_options(const shm::server_options& server_options, std::string client_id) {
    shm::client_options options;
    options.client_id = std::move(client_id);
    options.control.server_name = server_options.control_socket_name;
    options.control.server_abstract_namespace = server_options.control_socket_abstract_namespace;
    return options;
}

bool uds_bind_unavailable(const std::string& error) {
    return error.find("Operation not permitted") != std::string::npos &&
           error.find("bind UDS server") != std::string::npos;
}

bool shm_object_exists(const std::string& name) {
    const auto fd = ::shm_open(name.c_str(), O_RDWR, 0);
    if (fd < 0) {
        return false;
    }
    ::close(fd);
    return true;
}

template <typename Predicate> bool wait_until(Predicate&& done, std::chrono::milliseconds timeout = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return done();
}

} // namespace

TEST_CASE("SHM manager transport is not configured for non-SHM MQTT settings") {
    Everest::MQTTSettings mqtt_settings;
    mqtt_settings.broker_host = "localhost";
    mqtt_settings.broker_port = 1883;
    mqtt_settings.everest_prefix = "everest/";

    auto config = make_two_module_config(mqtt_settings);

    CHECK_FALSE(Everest::make_shm_server_options(config, mqtt_settings, 1234).has_value());
}

TEST_CASE("Module MQTT settings are unchanged for non-SHM transport") {
    Everest::MQTTSettings manager_settings;
    manager_settings.broker_host = "localhost";
    manager_settings.broker_port = 1883;
    manager_settings.everest_prefix = "everest/";
    manager_settings.external_prefix = "external/";

    const auto module_settings = Everest::make_module_mqtt_settings(manager_settings);

    CHECK(module_settings.broker_socket_path == manager_settings.broker_socket_path);
    CHECK(module_settings.broker_host == manager_settings.broker_host);
    CHECK(module_settings.broker_port == manager_settings.broker_port);
    CHECK(module_settings.framework_transport == manager_settings.framework_transport);
    CHECK(module_settings.shm_control_socket_path == manager_settings.shm_control_socket_path);
    CHECK(module_settings.everest_prefix == manager_settings.everest_prefix);
    CHECK(module_settings.external_prefix == manager_settings.external_prefix);
}

TEST_CASE("Module MQTT settings keep MQTT broker fields when SHM is selected") {
    Everest::MQTTSettings manager_settings;
    manager_settings.framework_transport = Everest::FrameworkTransportType::SHM;
    manager_settings.broker_host = "mqtt.example.test";
    manager_settings.broker_port = 1883;
    manager_settings.shm_control_socket_path = "everest-shm-control";
    manager_settings.shm_registered_topics = {"everest/config/request", "everest/modules/a/response"};
    manager_settings.everest_prefix = "everest/";
    manager_settings.external_prefix = "external/";

    const auto module_settings = Everest::make_module_mqtt_settings(manager_settings);

    CHECK(module_settings.shared_mem());
    CHECK(module_settings.broker_socket_path.empty());
    CHECK(module_settings.broker_host == "mqtt.example.test");
    CHECK(module_settings.shm_control_socket_path == manager_settings.shm_control_socket_path);
    CHECK(module_settings.shm_registered_topics == manager_settings.shm_registered_topics);
    CHECK(module_settings.broker_port == manager_settings.broker_port);
    CHECK(module_settings.everest_prefix == manager_settings.everest_prefix);
    CHECK(module_settings.external_prefix == manager_settings.external_prefix);
}

TEST_CASE("Module MQTT settings preserve external MQTT semantics while SHM is selected") {
    Everest::MQTTSettings manager_settings;
    manager_settings.framework_transport = Everest::FrameworkTransportType::SHM;
    manager_settings.broker_host = "mqtt.example.test";
    manager_settings.broker_port = 1883;
    manager_settings.shm_control_socket_path = "everest-shm-control";
    manager_settings.everest_prefix = "everest/";
    manager_settings.external_prefix = "external/api/";

    const auto module_settings = Everest::make_module_mqtt_settings(manager_settings);

    CHECK(module_settings.shared_mem());
    CHECK(module_settings.broker_socket_path.empty());
    CHECK(module_settings.broker_host == "mqtt.example.test");
    CHECK(module_settings.shm_control_socket_path == "everest-shm-control");
    CHECK(module_settings.everest_prefix == "everest/");
    CHECK(module_settings.external_prefix == "external/api/");
    CHECK(module_settings.broker_port == manager_settings.broker_port);
}

TEST_CASE("SHM manager transport uses deterministic unique endpoint names") {
    Everest::MQTTSettings mqtt_settings;
    mqtt_settings.framework_transport = Everest::FrameworkTransportType::SHM;
    mqtt_settings.everest_prefix = "test/everest/";

    CHECK(Everest::make_shm_transport_name(mqtt_settings, 1234) == "everest-shm-test-everest-1234");
    CHECK(Everest::make_shm_transport_name(mqtt_settings, 5678) == "everest-shm-test-everest-5678");
}

TEST_CASE("SHM manager transport creates server options for framework-local topics") {
    Everest::MQTTSettings mqtt_settings;
    mqtt_settings.framework_transport = Everest::FrameworkTransportType::SHM;
    mqtt_settings.shm_topic_registry_mode = Everest::ShmTopicRegistryMode::Static;
    mqtt_settings.everest_prefix = "everest/";
    mqtt_settings.external_prefix = "";

    auto config = make_two_module_config(mqtt_settings);
    const auto process_id = static_cast<int>(::getpid());
    const auto options = Everest::make_shm_server_options(config, mqtt_settings, process_id);

    REQUIRE(options.has_value());
    CHECK(mqtt_settings.shm_topic_registry_mode == Everest::ShmTopicRegistryMode::Static);
    CHECK(options->shm_name == "/everest-shm-everest-" + std::to_string(process_id));
    CHECK(options->segment_size == 0U);
    CHECK(options->topic_registry_capacity ==
          std::max(static_cast<std::uint32_t>(options->topics.size()), Everest::DEFAULT_SHM_TOPIC_REGISTRY_CAPACITY));
    CHECK(static_cast<std::size_t>(options->topic_registry_capacity) >= options->topics.size());
    CHECK(options->default_ring_slots == Everest::DEFAULT_SHM_TOPIC_SLOTS);
    CHECK(options->default_slot_size == Everest::DEFAULT_SHM_TOPIC_SLOT_SIZE);
    CHECK(options->control_socket_name == "everest-shm-everest-" + std::to_string(process_id));
    CHECK(options->control_socket_abstract_namespace);
    CHECK_THAT(options->control_socket_name, ContainsSubstring(std::to_string(process_id)));
    CHECK(has_topic(*options, "everest/config/request"));
    CHECK(has_topic(*options, "everest/modules/module_a/ready"));
    CHECK(has_topic(*options, "everest/modules/module_b/ready"));
    CHECK(has_topic(*options, "everest/modules/module_b/impl/impl1/var/a_var"));
    CHECK(has_topic(*options, "everest/modules/module_b/impl/impl1/cmd/a_cmd"));
    CHECK(has_topic(*options, "everest/modules/module_b/impl/impl1/cmd/a_cmd/response/module_a"));
    CHECK_FALSE(has_topic(*options, "everest/modules/module_b/impl/impl1/cmd/a_cmd/response/module_b"));
    CHECK_FALSE(options->topics.empty());

    const auto* command_topic = find_topic(*options, "everest/modules/module_b/impl/impl1/cmd/a_cmd");
    REQUIRE(command_topic != nullptr);
    CHECK(command_topic->total_slots == Everest::DEFAULT_SHM_TOPIC_SLOTS);
    CHECK(command_topic->slot_size == Everest::DEFAULT_SHM_TOPIC_SLOT_SIZE);

    everest::lib::io::shm::shm_server server(*options);
    const auto open_result = server.open();
    if (open_result.status == everest::lib::io::shm::io_status::resource_error &&
        open_result.message.find("Operation not permitted") != std::string::npos &&
        open_result.message.find("bind UDS server") != std::string::npos) {
        SKIP("UDS bind is not permitted in this environment");
    }
    INFO(open_result.message);
    REQUIRE(open_result.status == everest::lib::io::shm::io_status::ok);
    CHECK(server.is_open());

    const auto& effective_options = server.options();
    everest::lib::io::shm::shared_memory mapped(effective_options.shm_name,
                                                static_cast<std::size_t>(effective_options.segment_size), false);
    const auto* header = static_cast<const everest::lib::io::shm::SegmentHeader*>(mapped.get_ptr());
    CHECK(header->segment_size == effective_options.segment_size);
    CHECK(header->registry_entry_capacity == options->topic_registry_capacity);
    CHECK(server.close().status == everest::lib::io::shm::io_status::ok);
}

TEST_CASE("SHM manager transport rejects dynamic topic registry mode") {
    Everest::MQTTSettings mqtt_settings;
    mqtt_settings.framework_transport = Everest::FrameworkTransportType::SHM;
    mqtt_settings.shm_topic_registry_mode = Everest::ShmTopicRegistryMode::Dynamic;
    mqtt_settings.everest_prefix = "everest/";

    auto config = make_two_module_config(mqtt_settings);

    REQUIRE_THROWS_WITH(Everest::make_shm_server_options(config, mqtt_settings, static_cast<int>(::getpid())),
                        ContainsSubstring("shm_topic_registry_mode 'dynamic' is not implemented"));
}

TEST_CASE("SHM manager transport ignores module external_mqtt capability when building the SHM registry") {
    Everest::MQTTSettings mqtt_settings;
    mqtt_settings.framework_transport = Everest::FrameworkTransportType::SHM;
    mqtt_settings.everest_prefix = "everest/";
    mqtt_settings.external_prefix = "external/";

    auto config = make_two_module_config_with_external_mqtt(mqtt_settings);
    const auto options = Everest::make_shm_server_options(config, mqtt_settings, static_cast<int>(::getpid()));

    REQUIRE(config.get_manifests().at("TESTModuleB").value("enable_external_mqtt", false));
    REQUIRE(options.has_value());
    CHECK(has_topic(*options, "everest/modules/module_b/impl/impl1/cmd/a_cmd"));
    CHECK(has_topic(*options, "everest/modules/module_b/impl/impl1/var/a_var"));
    CHECK_FALSE(has_topic(*options, "external/modules/module_b/impl/impl1/cmd/a_cmd"));
    CHECK_FALSE(has_topic(*options, "external/modules/module_b/impl/impl1/var/a_var"));
    CHECK_FALSE(has_topic(*options, "external/config/request"));
    CHECK(std::none_of(options->topics.begin(), options->topics.end(),
                       [](const auto& topic) { return topic.name.rfind("external/", 0) == 0; }));
}

TEST_CASE("SHM manager transport ignores framework telemetry when building the SHM registry") {
    Everest::MQTTSettings mqtt_settings;
    mqtt_settings.framework_transport = Everest::FrameworkTransportType::SHM;
    mqtt_settings.everest_prefix = "everest/";
    mqtt_settings.external_prefix = "external/";

    auto config = make_two_module_config_with_telemetry(mqtt_settings);
    const auto options = Everest::make_shm_server_options(config, mqtt_settings, static_cast<int>(::getpid()));

    REQUIRE(config.get_manifests().at("TESTModuleB").value("enable_telemetry", false));
    REQUIRE(options.has_value());
    CHECK(has_topic(*options, "everest/modules/module_b/impl/impl1/cmd/a_cmd"));
    CHECK(has_topic(*options, "everest/modules/module_b/impl/impl1/var/a_var"));
    CHECK_FALSE(has_topic(*options, "telemetry/session/7/power"));
    CHECK(std::none_of(options->topics.begin(), options->topics.end(),
                       [](const auto& topic) { return topic.name.rfind("telemetry/", 0) == 0; }));
}

TEST_CASE("external_mqtt publish and subscribe use the MQTT side channel in SHM mode") {
    Everest::MQTTSettings mqtt_settings;
    mqtt_settings.framework_transport = Everest::FrameworkTransportType::SHM;
    mqtt_settings.broker_host = "mqtt.example.test";
    mqtt_settings.broker_port = 1883;
    mqtt_settings.shm_control_socket_path = "everest-shm-control";
    mqtt_settings.everest_prefix = "everest/";
    mqtt_settings.external_prefix = "external/";

    auto manager_config = make_two_module_config_with_external_mqtt(mqtt_settings);
    auto serialized = make_serialized_module_config(manager_config, "module_b");
    Everest::Config module_config(mqtt_settings, serialized);

    auto framework_transport = std::make_shared<Everest::tests::MockFrameworkTransport>("everest/", "external/");
    auto external_transport = std::make_shared<Everest::tests::MockFrameworkTransport>("everest/", "external/");

    Everest::Everest everest("module_b", module_config, false, framework_transport, "telemetry/", false, false,
                             external_transport);
    const auto framework_publish_count = framework_transport->published().size();

    everest.external_mqtt_publish("device/status", "online");
    const auto unsubscribe = everest.provide_external_mqtt_handler("device/command", [](const std::string&) {});
    everest.external_mqtt_publish("device/status", "still-online");

    REQUIRE(framework_transport->published().size() == framework_publish_count);
    REQUIRE(framework_transport->registered_handlers().count("external/device/command") == 0);
    REQUIRE(external_transport->connect_count() == 1);
    REQUIRE(external_transport->spawn_main_loop_thread_count() == 1);
    REQUIRE(external_transport->published().size() == 2);
    CHECK(external_transport->published().at(0).first == "external/device/status");
    CHECK(external_transport->published().at(0).second == "online");
    CHECK(external_transport->published().at(1).first == "external/device/status");
    CHECK(external_transport->published().at(1).second == "still-online");
    CHECK(external_transport->registered_handlers().count("external/device/command") == 1);

    unsubscribe();
    CHECK(external_transport->registered_handlers().count("external/device/command") == 0);
}

TEST_CASE("telemetry publish uses the MQTT side channel in SHM mode") {
    Everest::MQTTSettings mqtt_settings;
    mqtt_settings.framework_transport = Everest::FrameworkTransportType::SHM;
    mqtt_settings.broker_host = "mqtt.example.test";
    mqtt_settings.broker_port = 1883;
    mqtt_settings.shm_control_socket_path = "everest-shm-control";
    mqtt_settings.everest_prefix = "everest/";
    mqtt_settings.external_prefix = "external/";

    auto manager_config = make_two_module_config_with_telemetry(mqtt_settings);
    auto serialized = make_serialized_module_config(manager_config, "module_b");
    Everest::Config module_config(mqtt_settings, serialized);

    auto framework_transport = std::make_shared<Everest::tests::MockFrameworkTransport>("everest/", "external/");
    auto mqtt_side_channel = std::make_shared<Everest::tests::MockFrameworkTransport>("everest/", "external/");

    Everest::Everest everest("module_b", module_config, false, framework_transport, "telemetry/", true, false,
                             mqtt_side_channel);
    const auto framework_publish_count = framework_transport->published().size();

    everest.telemetry_publish("session", "power", "meter", Everest::TelemetryMap{{"voltage", 230.0}});

    REQUIRE(framework_transport->published().size() == framework_publish_count);
    REQUIRE(mqtt_side_channel->connect_count() == 1);
    REQUIRE(mqtt_side_channel->spawn_main_loop_thread_count() == 1);
    REQUIRE(mqtt_side_channel->published().size() == 1);
    CHECK(mqtt_side_channel->published().at(0).first == "telemetry/session/7/power");
    CHECK(mqtt_side_channel->published().at(0).second.at("msg_type") == "ExternalMQTT");
    const auto payload =
        nlohmann::json::parse(mqtt_side_channel->published().at(0).second.at("data").get<std::string>());
    CHECK(payload.at("connector_id") == 7);
    CHECK(payload.at("type") == "meter");
    CHECK(payload.at("voltage") == 230.0);
}

TEST_CASE("telemetry publish keeps MQTT framework transport behavior unchanged") {
    Everest::MQTTSettings mqtt_settings;
    mqtt_settings.framework_transport = Everest::FrameworkTransportType::MQTT;
    mqtt_settings.broker_host = "mqtt.example.test";
    mqtt_settings.broker_port = 1883;
    mqtt_settings.everest_prefix = "everest/";
    mqtt_settings.external_prefix = "external/";

    auto manager_config = make_two_module_config_with_telemetry(mqtt_settings);
    auto serialized = make_serialized_module_config(manager_config, "module_b");
    Everest::Config module_config(mqtt_settings, serialized);

    auto mqtt_transport = std::make_shared<Everest::tests::MockFrameworkTransport>("everest/", "external/");

    Everest::Everest everest("module_b", module_config, false, mqtt_transport, "telemetry/", true, false);
    const auto metadata_publish_count = mqtt_transport->published().size();

    Everest::ModuleAdapter adapter;
    adapter.telemetry_publish = [&everest](const std::string& category, const std::string& subcategory,
                                           const std::string& type, const Everest::TelemetryMap& telemetry) {
        everest.telemetry_publish(category, subcategory, type, telemetry);
    };
    Everest::TelemetryProvider provider(adapter);
    provider.publish("session", "power", "meter", Everest::TelemetryMap{{"current", 16}});

    REQUIRE(mqtt_transport->published().size() == metadata_publish_count + 1);
    CHECK(mqtt_transport->connect_count() == 0);
    CHECK(mqtt_transport->spawn_main_loop_thread_count() == 0);
    CHECK(mqtt_transport->published().back().first == "telemetry/session/7/power");
    CHECK(mqtt_transport->published().back().second.at("msg_type") == "ExternalMQTT");
    const auto payload = nlohmann::json::parse(mqtt_transport->published().back().second.at("data").get<std::string>());
    CHECK(payload.at("connector_id") == 7);
    CHECK(payload.at("type") == "meter");
    CHECK(payload.at("current") == 16);
}

TEST_CASE("SHM manager transport server starts and stops its event loop") {
    Everest::ShmManagerTransportServer server;
    const auto options = make_loop_test_options("start-stop");

    if (!server.start(options)) {
        if (uds_bind_unavailable(server.last_error_message())) {
            SKIP("UDS bind is not permitted in this environment");
        }
        FAIL("SHM manager transport server failed to start: " << server.last_error_message());
    }

    CHECK(server.is_running());
    shm::shm_client subscriber(make_client_options(options, "start-stop-subscriber"));
    REQUIRE(subscriber.connect().status == shm::io_status::ok);
    REQUIRE(subscriber.subscribe("everest/loop/topic", [](std::string_view, std::string_view) {}).status ==
            shm::io_status::ok);
    CHECK(subscriber.is_connected());
    CHECK(shm_object_exists(options.shm_name));
    CHECK(::access(options.control_socket_name.c_str(), F_OK) == 0);

    server.stop();
    CHECK_FALSE(server.is_running());
    CHECK_FALSE(shm_object_exists(options.shm_name));
    CHECK(::access(options.control_socket_name.c_str(), F_OK) != 0);

    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (subscriber.is_connected() && std::chrono::steady_clock::now() < deadline) {
        subscriber.sync();
        std::this_thread::sleep_for(1ms);
    }
    CHECK_FALSE(subscriber.is_connected());

    server.stop();
    CHECK_FALSE(server.is_running());
    CHECK_FALSE(shm_object_exists(options.shm_name));
    CHECK(::access(options.control_socket_name.c_str(), F_OK) != 0);
}

TEST_CASE("SHM manager transport server dispatches a publication on its background loop") {
    Everest::ShmManagerTransportServer server;
    const auto options = make_loop_test_options("dispatch");

    if (!server.start(options)) {
        if (uds_bind_unavailable(server.last_error_message())) {
            SKIP("UDS bind is not permitted in this environment");
        }
        FAIL("SHM manager transport server failed to start: " << server.last_error_message());
    }

    shm::shm_client subscriber(make_client_options(options, "dispatch-subscriber"));
    REQUIRE(subscriber.connect().status == shm::io_status::ok);

    std::string received_topic;
    std::string received_payload;
    const std::string topic = "everest/loop/topic";
    REQUIRE(subscriber
                .subscribe(topic,
                           [&](std::string_view topic_view, std::string_view payload) {
                               received_topic = std::string(topic_view);
                               received_payload = std::string(payload);
                           })
                .status == shm::io_status::ok);

    shm::shm_client publisher(make_client_options(options, "dispatch-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);
    REQUIRE(publisher.publish(topic, "payload").status == shm::io_status::ok);

    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (received_payload.empty() && std::chrono::steady_clock::now() < deadline) {
        subscriber.sync();
        std::this_thread::sleep_for(1ms);
    }

    CHECK(received_topic == topic);
    CHECK(received_payload == "payload");
    CHECK(server.counter_snapshot().messages_dispatched >= 1U);

    server.stop();
}

TEST_CASE("SHM manager transport replays retained payloads to late subscribers") {
    Everest::ShmManagerTransportServer server;
    const auto options = make_loop_test_options("retained-replay");

    if (!server.start(options)) {
        if (uds_bind_unavailable(server.last_error_message())) {
            SKIP("UDS bind is not permitted in this environment");
        }
        FAIL("SHM manager transport server failed to start: " << server.last_error_message());
    }

    const std::string topic = "everest/loop/topic";
    shm::shm_client publisher(make_client_options(options, "retained-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);

    shm::publish_options retained_options;
    retained_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    retained_options.retain = true;
    REQUIRE(publisher.publish(topic, "first", retained_options).status == shm::io_status::ok);
    REQUIRE(wait_until([&]() { return server.counter_snapshot().messages_published >= 1U; }));

    shm::shm_client late_subscriber(make_client_options(options, "retained-late-subscriber"));
    REQUIRE(late_subscriber.connect().status == shm::io_status::ok);
    std::vector<std::string> late_payloads;
    REQUIRE(
        late_subscriber
            .subscribe(topic, [&](std::string_view, std::string_view payload) { late_payloads.emplace_back(payload); })
            .status == shm::io_status::ok);
    REQUIRE(late_payloads == std::vector<std::string>{"first"});

    shm::publish_options non_retained_options;
    non_retained_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    REQUIRE(publisher.publish(topic, "transient", non_retained_options).status == shm::io_status::ok);
    REQUIRE(wait_until([&]() { return server.counter_snapshot().messages_published >= 2U; }));

    shm::shm_client second_late_subscriber(make_client_options(options, "retained-second-late-subscriber"));
    REQUIRE(second_late_subscriber.connect().status == shm::io_status::ok);
    std::vector<std::string> second_late_payloads;
    REQUIRE(second_late_subscriber
                .subscribe(topic, [&](std::string_view,
                                      std::string_view payload) { second_late_payloads.emplace_back(payload); })
                .status == shm::io_status::ok);
    REQUIRE(second_late_payloads == std::vector<std::string>{"first"});

    REQUIRE(publisher.publish(topic, "second", retained_options).status == shm::io_status::ok);
    REQUIRE(wait_until([&]() { return server.counter_snapshot().messages_published >= 3U; }));

    shm::shm_client replacement_subscriber(make_client_options(options, "retained-replacement-subscriber"));
    REQUIRE(replacement_subscriber.connect().status == shm::io_status::ok);
    std::vector<std::string> replacement_payloads;
    REQUIRE(replacement_subscriber
                .subscribe(topic, [&](std::string_view,
                                      std::string_view payload) { replacement_payloads.emplace_back(payload); })
                .status == shm::io_status::ok);
    REQUIRE(replacement_payloads == std::vector<std::string>{"second"});

    retained_options.clear_retained = true;
    REQUIRE(publisher.publish(topic, "", retained_options).status == shm::io_status::ok);
    REQUIRE(wait_until([&]() { return server.counter_snapshot().messages_published >= 4U; }));

    shm::shm_client cleared_subscriber(make_client_options(options, "retained-cleared-subscriber"));
    REQUIRE(cleared_subscriber.connect().status == shm::io_status::ok);
    std::vector<std::string> cleared_payloads;
    REQUIRE(cleared_subscriber
                .subscribe(topic,
                           [&](std::string_view, std::string_view payload) { cleared_payloads.emplace_back(payload); })
                .status == shm::io_status::ok);
    std::this_thread::sleep_for(10ms);
    CHECK(cleared_payloads.empty());

    server.stop();
}

TEST_CASE("SHM manager transport removes disconnected subscribers and releases blocked publishers") {
    Everest::ShmManagerTransportServer server;
    auto options = make_loop_test_options("subscriber-crash-release");
    options.topics.clear();
    options.topics.push_back({"everest/loop/topic", 1, 512});

    if (!server.start(options)) {
        if (uds_bind_unavailable(server.last_error_message())) {
            SKIP("UDS bind is not permitted in this environment");
        }
        FAIL("SHM manager transport server failed to start: " << server.last_error_message());
    }

    const std::string topic = "everest/loop/topic";
    shm::shm_client subscriber(make_client_options(options, "crash-release-subscriber"));
    REQUIRE(subscriber.connect().status == shm::io_status::ok);
    REQUIRE(subscriber.subscribe(topic, [](std::string_view, std::string_view) {}).status == shm::io_status::ok);
    REQUIRE(wait_until([&]() { return server.subscriber_snapshots(topic).size() == 1U; }));

    shm::shm_client publisher(make_client_options(options, "crash-release-publisher"));
    REQUIRE(publisher.connect().status == shm::io_status::ok);
    REQUIRE(publisher.publish(topic, "first").status == shm::io_status::ok);
    REQUIRE(wait_until([&]() { return server.counter_snapshot().messages_dispatched >= 1U; }));

    auto blocked_publish = std::async(std::launch::async, [&]() { return publisher.publish(topic, "second"); });
    REQUIRE(wait_until([&]() { return publisher.counter_snapshot().blocked_publish_attempts >= 1U; }));
    CHECK(blocked_publish.wait_for(0ms) != std::future_status::ready);

    REQUIRE(subscriber.disconnect().status == shm::io_status::ok);
    REQUIRE(wait_until([&]() {
        const auto counters = server.counter_snapshot();
        return counters.liveness_disconnects >= 1U && counters.subscriber_removals >= 1U &&
               counters.slots_released >= 1U && server.subscriber_snapshots(topic).empty();
    }));

    REQUIRE(blocked_publish.wait_for(3s) == std::future_status::ready);
    CHECK(blocked_publish.get().status == shm::io_status::ok);
    CHECK(server.subscriber_snapshots(topic).empty());

    REQUIRE(publisher.disconnect().status == shm::io_status::ok);
    server.stop();
    server.stop();
}

TEST_CASE("SHM manager transport start() fails without hanging when shm_server::open() fails") {
    Everest::ShmManagerTransportServer server;
    shm::server_options options;
    options.shm_name = "/everest-shm-manager-start-fail-" + std::to_string(::getpid());
    options.control_socket_name = "/tmp/everest-shm-manager-start-fail-" + std::to_string(::getpid());
    options.control_socket_abstract_namespace = false;
    options.unlink_shm_on_close = true;
    options.unlink_control_socket_on_close = true;
    // Force shm_server::open() to fire its error callback synchronously by passing options that
    // fail validation (oversized topic name). Before SHM-T-3.010 this re-entered the lifecycle
    // monitor while start() was still holding it and deadlocked.
    options.topics.push_back({std::string(shm::shm_topic_name_capacity + 1U, 'x'), 2, 256});

    auto started = std::async(std::launch::async, [&]() { return server.start(options); });
    REQUIRE(started.wait_for(5s) == std::future_status::ready);
    CHECK_FALSE(started.get());
    CHECK_FALSE(server.is_running());
    CHECK_THAT(server.last_error_message(), ContainsSubstring("SHM topic name exceeds registry name capacity"));
    server.stop();
}

TEST_CASE("SHM manager transport accepts long framework-style error topic names") {
    const std::string long_topic =
        "everest/modules/cb_bsp/impl/connector_lock/error/connector_lock/ConnectorLockCapNotCharged";
    REQUIRE(long_topic.size() > 64U);
    REQUIRE(long_topic.size() <= shm::shm_topic_name_capacity);

    Everest::ShmManagerTransportServer server;
    shm::server_options options;
    options.shm_name = "/everest-shm-manager-long-topic-" + std::to_string(::getpid());
    options.control_socket_name = "/tmp/everest-shm-manager-long-topic-" + std::to_string(::getpid());
    options.control_socket_abstract_namespace = false;
    options.unlink_shm_on_close = true;
    options.unlink_control_socket_on_close = true;
    options.topics.push_back({long_topic, 2, 512});

    if (!server.start(options)) {
        if (uds_bind_unavailable(server.last_error_message())) {
            SKIP("UDS bind is not permitted in this environment");
        }
        FAIL("SHM manager transport server failed to start with long topic: " << server.last_error_message());
    }
    CHECK(server.is_running());
    CHECK(server.subscriber_snapshots(long_topic).empty());
    server.stop();
    CHECK_FALSE(server.is_running());
}

TEST_CASE("make_shm_server_options accepts generated long error topics") {
    Everest::MQTTSettings mqtt_settings;
    mqtt_settings.framework_transport = Everest::FrameworkTransportType::SHM;
    mqtt_settings.everest_prefix = "everest/";
    mqtt_settings.external_prefix = "";

    auto config = make_two_module_config(mqtt_settings);
    const auto options = Everest::make_shm_server_options(config, mqtt_settings, static_cast<int>(::getpid()));
    REQUIRE(options.has_value());

    // Inject a long framework-style error topic and prove the resulting options pass shm_server validation.
    shm::server_options forwarded = *options;
    const std::string long_topic =
        "everest/modules/cb_bsp/impl/connector_lock/error/connector_lock/ConnectorLockCapNotCharged";
    REQUIRE(long_topic.size() > 64U);
    REQUIRE(long_topic.size() <= shm::shm_topic_name_capacity);
    forwarded.topics.push_back({long_topic, Everest::DEFAULT_SHM_TOPIC_SLOTS, Everest::DEFAULT_SHM_TOPIC_SLOT_SIZE});
    forwarded.topic_registry_capacity =
        std::max(forwarded.topic_registry_capacity, static_cast<std::uint32_t>(forwarded.topics.size()));
    forwarded.shm_name = "/everest-shm-mgr-options-long-" + std::to_string(::getpid());
    forwarded.control_socket_name = "/tmp/everest-shm-mgr-options-long-" + std::to_string(::getpid());
    forwarded.control_socket_abstract_namespace = false;
    forwarded.unlink_shm_on_close = true;
    forwarded.unlink_control_socket_on_close = true;

    shm::shm_server server(forwarded);
    const auto open_result = server.open();
    if (open_result.status == shm::io_status::resource_error && uds_bind_unavailable(open_result.message)) {
        SKIP("UDS bind is not permitted in this environment");
    }
    INFO(open_result.message);
    REQUIRE(open_result.status == shm::io_status::ok);
    CHECK(server.is_open());
    CHECK(server.close().status == shm::io_status::ok);
}
