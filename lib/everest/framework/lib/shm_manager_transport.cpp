// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <utils/shm_manager_transport.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

#include <fmt/format.h>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/logging.hpp>
#include <everest/util/async/monitor.hpp>

namespace Everest {
namespace {

namespace shm = everest::lib::io::shm;

std::string sanitize_name_part(std::string value) {
    std::replace_if(
        value.begin(), value.end(), [](unsigned char c) { return !std::isalnum(c); }, '-');
    while (!value.empty() && value.back() == '-') {
        value.pop_back();
    }
    return value.empty() ? "everest" : value;
}

void add_topic(std::set<std::string>& topics, std::string topic) {
    if (!topic.empty()) {
        topics.insert(std::move(topic));
    }
}

void add_error_topic(std::set<std::string>& topics, ConfigBase& config, const std::string& module_id,
                     const std::string& impl_id, const std::string& error_namespace, const std::string& error_name) {
    add_topic(topics,
              fmt::format("{}/error/{}/{}", config.mqtt_prefix(module_id, impl_id), error_namespace, error_name));
}

void add_error_reference_topic(std::set<std::string>& topics, ConfigBase& config, const std::string& module_id,
                               const std::string& impl_id, const std::string& reference) {
    const std::string prefix = "/errors/";
    const std::string separator = "#/";
    if (reference.rfind(prefix, 0) != 0) {
        return;
    }

    const auto separator_pos = reference.find(separator);
    if (separator_pos == std::string::npos) {
        return;
    }

    const auto namespace_pos = prefix.size();
    const auto error_namespace = reference.substr(namespace_pos, separator_pos - namespace_pos);
    const auto error_name = reference.substr(separator_pos + separator.size());
    if (!error_namespace.empty() && !error_name.empty()) {
        add_error_topic(topics, config, module_id, impl_id, error_namespace, error_name);
    }
}

void add_error_topics(std::set<std::string>& topics, ConfigBase& config, const std::string& module_id,
                      const std::string& impl_id, const nlohmann::json& interface_definition) {
    if (!interface_definition.contains("errors")) {
        return;
    }

    const auto& errors = interface_definition.at("errors");
    if (errors.is_array()) {
        for (const auto& error_reference : errors) {
            if (error_reference.is_object() && error_reference.contains("reference") &&
                error_reference.at("reference").is_string()) {
                add_error_reference_topic(topics, config, module_id, impl_id,
                                          error_reference.at("reference").get<std::string>());
            }
        }
        return;
    }

    for (const auto& error_namespace : errors.items()) {
        if (error_namespace.value().is_array()) {
            for (const auto& error_type : error_namespace.value()) {
                if (error_type.is_string()) {
                    add_error_topic(topics, config, module_id, impl_id, error_namespace.key(),
                                    error_type.get<std::string>());
                } else if (error_type.is_object() && error_type.contains("name") && error_type.at("name").is_string()) {
                    add_error_topic(topics, config, module_id, impl_id, error_namespace.key(),
                                    error_type.at("name").get<std::string>());
                }
            }
            continue;
        }

        for (const auto& error_type : error_namespace.value().items()) {
            const auto error_name = error_type.value().is_object() && error_type.value().contains("name") &&
                                            error_type.value().at("name").is_string()
                                        ? error_type.value().at("name").get<std::string>()
                                        : error_type.key();
            add_error_topic(topics, config, module_id, impl_id, error_namespace.key(), error_name);
        }
    }
}

std::set<std::string> connected_origin_module_ids(ManagerConfig& config, const std::string& provider_module_id,
                                                  const std::string& provider_impl_id) {
    std::set<std::string> origins;
    for (const auto& [origin_module_id, module_config] : config.get_module_configurations()) {
        for (const auto& [_, fulfillments] : module_config.connections) {
            for (const auto& fulfillment : fulfillments) {
                if (fulfillment.module_id == provider_module_id && fulfillment.implementation_id == provider_impl_id) {
                    origins.insert(origin_module_id);
                }
            }
        }
    }
    return origins;
}

void add_framework_topics(std::set<std::string>& topics, ManagerConfig& config, const MQTTSettings& mqtt_settings) {
    const auto& everest_prefix = mqtt_settings.everest_prefix;

    add_topic(topics, fmt::format("{}config/request", everest_prefix));
    add_topic(topics, fmt::format("{}interfaces", everest_prefix));
    add_topic(topics, fmt::format("{}types", everest_prefix));
    add_topic(topics, fmt::format("{}settings", everest_prefix));
    add_topic(topics, fmt::format("{}module_names", everest_prefix));
    add_topic(topics, fmt::format("{}ready", everest_prefix));

    if (mqtt_settings.shared_mem() && config.get_settings().value("validate_schema", false)) {
        add_topic(topics, fmt::format("{}schemas", everest_prefix));
    }

    for (const auto& interface_definition : config.get_interface_definitions().items()) {
        add_topic(topics, fmt::format("{}interface_definitions/{}", everest_prefix, interface_definition.key()));
    }

    for (const auto& type_definition : config.get_types().items()) {
        add_topic(topics, fmt::format("{}type_definitions/{}", everest_prefix, type_definition.key()));
    }

    for (const auto& manifest : config.get_manifests().items()) {
        add_topic(topics, fmt::format("{}manifests/{}", everest_prefix, manifest.key()));
    }
}

void add_module_topics(std::set<std::string>& topics, ManagerConfig& config) {
    const auto module_names = config.get_module_names();
    for (const auto& [module_id, module_name] : module_names) {
        add_topic(topics, fmt::format("{}/response", config.mqtt_module_prefix(module_id)));
        add_topic(topics, fmt::format("{}/ready", config.mqtt_module_prefix(module_id)));
        add_topic(topics, fmt::format("{}/metadata", config.mqtt_module_prefix(module_id)));
        add_topic(topics, fmt::format("{}/heartbeat", config.mqtt_module_prefix(module_id)));

        const auto& manifest = config.get_manifests().at(module_name);
        if (!manifest.contains("provides")) {
            continue;
        }

        for (const auto& provides : manifest.at("provides").items()) {
            const auto& impl_id = provides.key();
            const auto& interface_definition =
                config.get_interface_definitions().at(provides.value().at("interface").get<std::string>());

            if (interface_definition.contains("vars")) {
                for (const auto& var : interface_definition.at("vars").items()) {
                    add_topic(topics, fmt::format("{}/var/{}", config.mqtt_prefix(module_id, impl_id), var.key()));
                }
            }

            if (interface_definition.contains("cmds")) {
                for (const auto& cmd : interface_definition.at("cmds").items()) {
                    const auto cmd_topic = fmt::format("{}/cmd/{}", config.mqtt_prefix(module_id, impl_id), cmd.key());
                    add_topic(topics, cmd_topic);
                    for (const auto& origin_module_id : connected_origin_module_ids(config, module_id, impl_id)) {
                        add_topic(topics, fmt::format("{}/response/{}", cmd_topic, origin_module_id));
                    }
                }
            }

            add_error_topics(topics, config, module_id, impl_id, interface_definition);
        }
    }
}

std::uint32_t shm_topic_slots(const MQTTSettings& mqtt_settings) {
    return mqtt_settings.shm_topic_slots == 0 ? DEFAULT_SHM_TOPIC_SLOTS : mqtt_settings.shm_topic_slots;
}

std::uint32_t shm_topic_slot_size(const MQTTSettings& mqtt_settings) {
    return mqtt_settings.shm_topic_slot_size == 0 ? DEFAULT_SHM_TOPIC_SLOT_SIZE : mqtt_settings.shm_topic_slot_size;
}

std::uint32_t shm_topic_registry_capacity(const MQTTSettings& mqtt_settings) {
    return mqtt_settings.shm_topic_registry_capacity == 0 ? DEFAULT_SHM_TOPIC_REGISTRY_CAPACITY
                                                          : mqtt_settings.shm_topic_registry_capacity;
}

bool use_abstract_namespace_for_control_socket(const std::string& control_socket_name) {
    return control_socket_name.empty() || control_socket_name.front() != '/';
}

} // namespace

std::string make_shm_transport_name(const MQTTSettings& mqtt_settings, int process_id) {
    return fmt::format("everest-shm-{}-{}", sanitize_name_part(mqtt_settings.everest_prefix), process_id);
}

MQTTSettings make_module_mqtt_settings(const MQTTSettings& manager_mqtt_settings) {
    return manager_mqtt_settings;
}

std::optional<shm::server_options> make_shm_server_options(ManagerConfig& config, const MQTTSettings& mqtt_settings,
                                                           int process_id) {
    if (!mqtt_settings.shared_mem()) {
        return std::nullopt;
    }
    if (mqtt_settings.shm_topic_registry_mode == ShmTopicRegistryMode::Dynamic) {
        throw std::runtime_error("shm_topic_registry_mode 'dynamic' is not implemented. The SHM Manager transport "
                                 "only supports the static Manager-precomputed topic registry.");
    }

    const auto transport_name = make_shm_transport_name(mqtt_settings, process_id);
    std::set<std::string> topics;
    add_framework_topics(topics, config, mqtt_settings);
    add_module_topics(topics, config);

    shm::server_options options;
    options.shm_name = "/" + transport_name;
    options.topic_registry_capacity =
        std::max(static_cast<std::uint32_t>(topics.size()), shm_topic_registry_capacity(mqtt_settings));
    options.default_ring_slots = shm_topic_slots(mqtt_settings);
    options.default_slot_size = shm_topic_slot_size(mqtt_settings);
    options.control_socket_name =
        mqtt_settings.shm_control_socket_path.empty() ? transport_name : mqtt_settings.shm_control_socket_path;
    options.control_socket_abstract_namespace = use_abstract_namespace_for_control_socket(options.control_socket_name);
    options.unlink_shm_on_close = true;
    options.unlink_control_socket_on_close = true;
    options.topics.reserve(topics.size());
    for (const auto& topic : topics) {
        options.topics.push_back({topic, shm_topic_slots(mqtt_settings), shm_topic_slot_size(mqtt_settings)});
    }

    return options;
}

struct ShmManagerTransportServer::Impl {
    enum class lifecycle_state {
        stopped,
        starting,
        running,
        stopping,
    };

    struct State {
        lifecycle_state lifecycle{lifecycle_state::stopped};
        std::unique_ptr<shm::shm_server> server;
        std::unique_ptr<everest::lib::io::event::fd_event_handler> event_handler;
        std::thread loop;
        std::atomic_bool running{false};
    };

    bool start(shm::server_options options);
    void stop();
    bool is_running() const;
    std::string last_error_message() const;
    shm::transport_counter_snapshot counter_snapshot() const;
    std::vector<shm::subscriber_snapshot> subscriber_snapshots() const;
    std::vector<shm::subscriber_snapshot> subscriber_snapshots(std::string_view topic) const;
    void set_error_message(std::string message);

    mutable everest::lib::util::monitor<State> state;
    // Tracked separately from the lifecycle monitor so the SHM server error callback can update the
    // recorded error without re-entering the (non-recursive) lifecycle monitor that start()/stop() hold.
    mutable std::mutex last_error_mutex;
    std::string last_error;
};

bool ShmManagerTransportServer::Impl::start(shm::server_options options) {
    std::unique_ptr<everest::lib::io::event::fd_event_handler> event_handler;
    std::unique_ptr<shm::shm_server> server;

    {
        auto guarded = state.handle();
        guarded.wait([&guarded]() {
            return guarded->lifecycle != lifecycle_state::starting && guarded->lifecycle != lifecycle_state::stopping;
        });

        if (guarded->lifecycle == lifecycle_state::running) {
            return true;
        }

        guarded->lifecycle = lifecycle_state::starting;
        guarded->running = false;
        event_handler = std::make_unique<everest::lib::io::event::fd_event_handler>();
        server = std::make_unique<shm::shm_server>(std::move(options));
        server->set_error_handler([this](shm::io_status status, std::string_view message) {
            if (status != shm::io_status::ok) {
                auto error = fmt::format("{} {}", shm::to_string(status), message);
                set_error_message(error);
                EVLOG_error << "SHM server error: " << error;
            }
        });
    }

    // Run open()/register_events() without holding the lifecycle monitor: the SHM server's error
    // callback writes through set_error_message() which uses its own mutex, so this section is
    // re-entrant with the callback path even when open() fails synchronously.
    const auto open_result = server->open();
    if (open_result.status != shm::io_status::ok) {
        EVLOG_critical << "Could not start SHM transport control endpoint: " << shm::to_string(open_result.status)
                       << " " << open_result.message;
        server.reset();
        event_handler.reset();
        {
            auto guarded = state.handle();
            guarded->lifecycle = lifecycle_state::stopped;
            guarded->running = false;
            state.notify_all();
        }
        return false;
    }

    if (!server->register_events(*event_handler)) {
        set_error_message("Failed to register SHM transport server events");
        EVLOG_critical << "Could not register SHM transport control endpoint with event handler";
        (void)server->close();
        server.reset();
        event_handler.reset();
        {
            auto guarded = state.handle();
            guarded->lifecycle = lifecycle_state::stopped;
            guarded->running = false;
            state.notify_all();
        }
        return false;
    }

    std::thread loop;
    std::atomic_bool* running_flag = nullptr;
    auto* raw_event_handler = event_handler.get();
    {
        auto guarded = state.handle();
        guarded->event_handler = std::move(event_handler);
        guarded->server = std::move(server);
        guarded->running = true;
        running_flag = &guarded->running;
    }

    std::unique_ptr<shm::shm_server> server_to_close;
    std::string thread_start_error;
    try {
        loop = std::thread([raw_event_handler, running_flag]() { raw_event_handler->run(*running_flag); });
    } catch (const std::exception& e) {
        thread_start_error = fmt::format("Failed to start SHM transport event loop: {}", e.what());
        auto guarded = state.handle();
        guarded->running = false;
        if (guarded->server != nullptr && guarded->event_handler != nullptr) {
            (void)guarded->server->unregister_events(*guarded->event_handler);
        }
        server_to_close = std::move(guarded->server);
        guarded->event_handler.reset();
    }

    if (!thread_start_error.empty()) {
        if (server_to_close != nullptr) {
            (void)server_to_close->close();
        }
        {
            auto guarded = state.handle();
            guarded->lifecycle = lifecycle_state::stopped;
            guarded->running = false;
            state.notify_all();
        }
        set_error_message(thread_start_error);
        EVLOG_critical << thread_start_error;
        return false;
    }

    auto guarded = state.handle();
    guarded->loop = std::move(loop);
    guarded->lifecycle = lifecycle_state::running;
    state.notify_all();
    return true;
}

void ShmManagerTransportServer::Impl::stop() {
    std::thread loop_to_join;
    std::unique_ptr<shm::shm_server> server_to_close;

    {
        auto guarded = state.handle();
        guarded.wait([&guarded]() { return guarded->lifecycle != lifecycle_state::starting; });
        if (guarded->lifecycle == lifecycle_state::stopping) {
            guarded.wait([&guarded]() { return guarded->lifecycle != lifecycle_state::stopping; });
        }
        if (guarded->lifecycle == lifecycle_state::stopped) {
            return;
        }

        guarded->lifecycle = lifecycle_state::stopping;
        guarded->running = false;
        if (guarded->event_handler != nullptr) {
            guarded->event_handler->add_action([]() {});
        }
        if (guarded->loop.joinable()) {
            loop_to_join = std::move(guarded->loop);
        }
    }

    if (loop_to_join.joinable()) {
        loop_to_join.join();
    }

    {
        auto guarded = state.handle();
        if (guarded->server != nullptr) {
            if (guarded->event_handler != nullptr) {
                (void)guarded->server->unregister_events(*guarded->event_handler);
            }
            server_to_close = std::move(guarded->server);
        }
        guarded->event_handler.reset();
    }

    if (server_to_close != nullptr) {
        const auto result = server_to_close->close();
        if (result.status != shm::io_status::ok) {
            auto error = fmt::format("{} {}", shm::to_string(result.status), result.message);
            set_error_message(error);
            EVLOG_error << "SHM transport close failed: " << error;
        }
    }

    {
        auto guarded = state.handle();
        guarded->running = false;
        guarded->lifecycle = lifecycle_state::stopped;
        state.notify_all();
    }
}

bool ShmManagerTransportServer::Impl::is_running() const {
    return state.handle()->lifecycle == lifecycle_state::running;
}

std::string ShmManagerTransportServer::Impl::last_error_message() const {
    std::lock_guard<std::mutex> lock(last_error_mutex);
    return last_error;
}

shm::transport_counter_snapshot ShmManagerTransportServer::Impl::counter_snapshot() const {
    auto guarded = state.handle();
    if (guarded->lifecycle != lifecycle_state::running || guarded->server == nullptr) {
        return {};
    }
    return guarded->server->counter_snapshot();
}

std::vector<shm::subscriber_snapshot> ShmManagerTransportServer::Impl::subscriber_snapshots() const {
    auto guarded = state.handle();
    if (guarded->lifecycle != lifecycle_state::running || guarded->server == nullptr) {
        return {};
    }
    return guarded->server->subscriber_snapshots();
}

std::vector<shm::subscriber_snapshot>
ShmManagerTransportServer::Impl::subscriber_snapshots(std::string_view topic) const {
    auto guarded = state.handle();
    if (guarded->lifecycle != lifecycle_state::running || guarded->server == nullptr) {
        return {};
    }
    return guarded->server->subscriber_snapshots(topic);
}

void ShmManagerTransportServer::Impl::set_error_message(std::string message) {
    std::lock_guard<std::mutex> lock(last_error_mutex);
    last_error = std::move(message);
}

ShmManagerTransportServer::ShmManagerTransportServer() : m_impl(std::make_unique<Impl>()) {
}

ShmManagerTransportServer::~ShmManagerTransportServer() {
    if (m_impl != nullptr) {
        m_impl->stop();
    }
}

ShmManagerTransportServer::ShmManagerTransportServer(ShmManagerTransportServer&&) noexcept = default;
ShmManagerTransportServer& ShmManagerTransportServer::operator=(ShmManagerTransportServer&&) noexcept = default;

bool ShmManagerTransportServer::start(shm::server_options options) {
    return m_impl->start(std::move(options));
}

void ShmManagerTransportServer::stop() {
    m_impl->stop();
}

bool ShmManagerTransportServer::is_running() const {
    return m_impl->is_running();
}

std::string ShmManagerTransportServer::last_error_message() const {
    return m_impl->last_error_message();
}

shm::transport_counter_snapshot ShmManagerTransportServer::counter_snapshot() const {
    return m_impl->counter_snapshot();
}

std::vector<shm::subscriber_snapshot> ShmManagerTransportServer::subscriber_snapshots() const {
    return m_impl->subscriber_snapshots();
}

std::vector<shm::subscriber_snapshot> ShmManagerTransportServer::subscriber_snapshots(std::string_view topic) const {
    return m_impl->subscriber_snapshots(topic);
}

} // namespace Everest
