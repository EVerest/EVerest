// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "include/lifecycle_api.hpp"
#include <everest/logging.hpp>
#include <utils/date.hpp>

#include "include/lifecycle_type_wrapper.hpp"
#include <api_request_handler.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/lifecycle/API.hpp>
#include <everest_api_types/lifecycle/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>

#include <generated/version_information.hpp>

namespace API_types = ev_API::V1_0::types;
namespace API_types_ext = API_types::lifecycle;
namespace API_generic = API_types::generic;
namespace API_wrapper = ::Everest::api::types::lifecycle;

namespace {

// Convert ConfigurationApiStatus to ConfigurationApiAvailability
inline API_types_ext::ConfigurationApiAvailability
to_configuration_api_availability(const ::Everest::api::lifecycle::ConfigurationApiStatus& status) {
    switch (status) {
    case ::Everest::api::lifecycle::ConfigurationApiStatus::NotAvailable:
        return API_types_ext::ConfigurationApiAvailability::N_A;
    case ::Everest::api::lifecycle::ConfigurationApiStatus::AvailableRO:
        return API_types_ext::ConfigurationApiAvailability::RO;
    case ::Everest::api::lifecycle::ConfigurationApiStatus::AvailableRW:
        return API_types_ext::ConfigurationApiAvailability::RW;
    }
    return API_types_ext::ConfigurationApiAvailability::N_A;
}

} // namespace

namespace Everest::api::lifecycle {

const ev_API::Topics LifecycleAPI::Lwt::topics = [] {
    ev_API::Topics topics;
    topics.setup("_unused_", "lifecycle", 1);
    return topics;
}();

std::string LifecycleAPI::Lwt::get_data() {
    API_types_ext::ExecutionStatusUpdateNotice lwt_status_update{};
    lwt_status_update.everest_running = false;
    return serialize(lwt_status_update);
}

std::string LifecycleAPI::Lwt::get_topic() {
    return topics.nonmodule_to_extern("status");
}

namespace {

/// \brief Publish a lifecycle status with the flags every status update on this topic must carry.
///
/// retain: a client connecting later must immediately learn the current status.
/// record_retained = false: the retained status has to survive the manager's clear_retained_topics().
void publish_status(MQTTAbstraction& mqtt_abstraction, const std::string& payload) {
    static const std::string topic = LifecycleAPI::Lwt::get_topic();
    mqtt_abstraction.publish(topic, payload, QOS::QOS2, true, false);
}

} // namespace

void LifecycleAPI::publish_shutdown_status(MQTTAbstraction& mqtt_abstraction) {
    publish_status(mqtt_abstraction, Lwt::get_data());
}

LifecycleAPI::LifecycleAPI(MQTTAbstraction& mqtt_abstraction, ::Everest::config::ConfigServiceInterface& config_service,
                           ConfigurationApiStatus config_api_availability, bool readonly,
                           std::function<StopModulesResult()> stop_fn,
                           std::function<RestartModulesResult()> restart_fn) :
    m_mqtt_abstraction(mqtt_abstraction),
    m_config_service(config_service),
    m_config_api_availability(config_api_availability),
    m_readonly(readonly),
    stop_fn_(std::move(stop_fn)),
    restart_fn_(std::move(restart_fn)) {

    generate_api_cmd_stop_modules();
    generate_api_cmd_start_modules();
    generate_api_cmd_get_everest_version();

    generate_api_var_status();
}

LifecycleAPI::~LifecycleAPI() {
    // The manager stops the MQTT message-handler worker threads before destroying this object, so no
    // handler can be running here. Unsubscribe every topic we registered.
    for (const auto& [topic, token] : m_registered_handlers) {
        m_mqtt_abstraction.unregister_handler(topic, token);
    }
}

StopModulesResult LifecycleAPI::stop_modules() {
    if (stop_fn_) {
        return stop_fn_();
    }
    return StopModulesResult::Rejected;
}

RestartModulesResult LifecycleAPI::restart_modules() {
    if (restart_fn_) {
        return restart_fn_();
    }
    return RestartModulesResult::Rejected;
}

void LifecycleAPI::generate_api_cmd_stop_modules() {
    subscribe_api_topic("stop_modules", [this](std::string const& data) {
        return handle_request(
            m_mqtt_abstraction, data, "stop_modules",
            API_types_ext::StopModulesResult{API_types_ext::StopModulesResultEnum::Rejected},
            [this](API_generic::RequestReply const& msg) {
                if (m_readonly) {
                    publish_reply(m_mqtt_abstraction, msg,
                                  API_types_ext::StopModulesResult{API_types_ext::StopModulesResultEnum::Rejected});
                } else {
                    auto res = stop_modules();
                    publish_reply(m_mqtt_abstraction, msg,
                                  API_types_ext::StopModulesResult{API_wrapper::to_external_api(res)});
                }
                return true;
            });
    });
}

void LifecycleAPI::generate_api_cmd_start_modules() {
    subscribe_api_topic("start_modules", [this](std::string const& data) {
        return handle_request(
            m_mqtt_abstraction, data, "start_modules",
            API_types_ext::StartModulesResult{API_types_ext::StartModulesResultEnum::Rejected},
            [this](API_generic::RequestReply const& msg) {
                if (m_readonly) {
                    publish_reply(m_mqtt_abstraction, msg,
                                  API_types_ext::StartModulesResult{API_types_ext::StartModulesResultEnum::Rejected});
                } else {
                    auto res = restart_modules();
                    publish_reply(m_mqtt_abstraction, msg,
                                  API_types_ext::StartModulesResult{API_wrapper::to_external_api(res)});
                }
                return true;
            });
    });
}

void LifecycleAPI::generate_api_cmd_get_everest_version() {

    subscribe_api_topic("get_everest_version", [this](std::string const& data) {
        API_types_ext::EVerestVersion everest_version;
        everest_version.name = PROJECT_NAME;
        everest_version.version = PROJECT_VERSION;
        everest_version.git_version = GIT_VERSION;

        // EVerestVersion has no failure shape; the version is always available, so it doubles as
        // the failure reply for the (only theoretical) exception path.
        return handle_request(m_mqtt_abstraction, data, "get_everest_version", everest_version,
                              [this, &everest_version](API_generic::RequestReply const& msg) {
                                  publish_reply(m_mqtt_abstraction, msg, everest_version);
                                  return true;
                              });
    });
}

void LifecycleAPI::generate_api_var_status() {
    using SrcT = ::Everest::config::ActiveSlotStatus;
    using TarT = ::everest::lib::API::V1_0::types::lifecycle::ModuleExecutionStatusEnum;
    // indicate on the API that EVerest is alive
    publish_execution_status(Everest::Date::to_rfc3339(date::utc_clock::now()), TarT::NotRunning);

    // setup updates
    m_config_service.register_active_slot_update_handler([this](const Everest::config::ActiveSlotUpdate& update) {
        // Initialized to keep the compiler happy: the switch below has no default (so -Wswitch flags a
        // newly added ActiveSlotStatus), but is exhaustive, so this value is never actually published.
        TarT module_status = TarT::NotRunning;

        // Mapping is explicit per internal enum value (no default) so a newly added ActiveSlotStatus
        // fails the build here instead of being silently dropped. Every current status maps to a
        // client-visible one; in particular FailedToStart must reach a client that was told "Starting".
        switch (update.status) {
        case SrcT::Running:
            module_status = TarT::Running;
            break;
        case SrcT::Stopped:
            module_status = TarT::NotRunning;
            break;
        case SrcT::Starting:
            module_status = TarT::Starting;
            break;
        case SrcT::Stopping:
            module_status = TarT::Stopping;
            break;
        case SrcT::FailedToStart:
            module_status = TarT::FailedToStart;
            break;
        case SrcT::RestartTriggered:
            module_status = TarT::RestartTriggered;
            break;
        }
        if (update.status == m_last_module_status) {
            return;
        }
        m_last_module_status = update.status;

        publish_execution_status(update.timestamp, module_status);
    });
}

void LifecycleAPI::publish_execution_status(const std::string& tstamp,
                                            API_types_ext::ModuleExecutionStatusEnum module_status) {
    const auto cfg_api_availability = to_configuration_api_availability(m_config_api_availability);

    API_types_ext::ExecutionStatusUpdateNotice exec_status_update{};
    exec_status_update.tstamp = tstamp;
    exec_status_update.everest_running = true;
    exec_status_update.configuration_api_available = cfg_api_availability;
    exec_status_update.lifecycle_api_ro = m_readonly;

    exec_status_update.module_status = module_status;

    publish_status(m_mqtt_abstraction, serialize(exec_status_update));
}

void LifecycleAPI::subscribe_api_topic(std::string const& var, ParseAndPublishFtor const& parse_and_publish) {
    auto topic = Lwt::topics.extern_to_nonmodule(var);
    auto handler = std::make_shared<TypedHandler>(
        HandlerType::ExternalMQTT, std::make_shared<Handler>([=](std::string const& topic, nlohmann::json data) {
            try {
                if (not parse_and_publish(data)) {
                    EVLOG_warning << "Invalid data: Deserialization failed.\n" << topic << "\n" << data;
                }
            } catch (const std::exception& e) {
                EVLOG_warning << "Topic: '" << topic << "' failed with -> " << e.what() << "\n => " << data;
            } catch (...) {
                EVLOG_warning << "Invalid data: Failed to parse JSON or to get data from it.\n" << topic;
            }
        }));
    m_mqtt_abstraction.register_handler(topic, handler, QOS::QOS2);
    m_registered_handlers.emplace_back(topic, handler);
}
} // namespace Everest::api::lifecycle
