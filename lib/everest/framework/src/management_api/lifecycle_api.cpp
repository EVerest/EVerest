// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "include/lifecycle_api.hpp"
#include <everest/logging.hpp>
#include <utils/date.hpp>

#include "include/lifecycle_type_wrapper.hpp"
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
using ev_API::deserialize;


LifecycleAPI::LifecycleAPI(MQTTAbstraction& mqtt_abstraction, ::Everest::config::ConfigServiceInterface& config_service,
                           ConfigurationApiStatus config_api_availability, bool readonly,
                           std::function<StopModulesResult()> stop_fn,
                           std::function<RestartModulesResult()> restart_fn) :
    m_mqtt_abstraction(mqtt_abstraction),
    // TODO(CB): If we don't need m_config_service anymore, remove it (but maybe we want to publish the active_slot id?)
    m_config_service(config_service),
    m_config_api_availability(config_api_availability),
    m_readonly(readonly),
    stop_fn_(std::move(stop_fn)),
    restart_fn_(std::move(restart_fn)) {

    m_topics.setup("_unused_", "lifecycle", 0);

    generate_api_cmd_stop_modules();
    generate_api_cmd_start_modules();
    generate_api_cmd_get_everest_version();

    generate_api_var_status();
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
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            if (m_readonly) {
                API_types_ext::StopModulesResult ext_res{API_types_ext::StopModulesResultEnum::Rejected};
                m_mqtt_abstraction.publish(msg.replyTo, serialize(ext_res));
            } else {
                auto res = stop_modules();
                API_types_ext::StopModulesResult ext_res{API_wrapper::to_external_api(res)};
                m_mqtt_abstraction.publish(msg.replyTo, serialize(ext_res));
            }
            return true;
        }
        EVLOG_warning << "Failed to deserialize stop_modules request.";
        return false;
    });
}

void LifecycleAPI::generate_api_cmd_start_modules() {
    subscribe_api_topic("start_modules", [this](std::string const& data) {
        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {
            if (m_readonly) {
                API_types_ext::StartModulesResult ext_res{API_types_ext::StartModulesResultEnum::Rejected};
                m_mqtt_abstraction.publish(msg.replyTo, serialize(ext_res));
            } else {
                auto res = restart_modules();
                API_types_ext::StartModulesResult ext_res{API_wrapper::to_external_api(res)};
                m_mqtt_abstraction.publish(msg.replyTo, serialize(ext_res));
            }
            return true;
        }
        EVLOG_warning << "Failed to deserialize start_modules request.";
        return false;
    });
}

void LifecycleAPI::generate_api_cmd_get_everest_version() {

    subscribe_api_topic("get_everest_version", [this](std::string const& data) {
        static everest::lib::API::V1_0::types::lifecycle::EVerestVersion everest_version;

        everest_version.name = PROJECT_NAME;
        everest_version.version = PROJECT_VERSION;
        everest_version.git_version = GIT_VERSION;

        API_generic::RequestReply msg;
        if (deserialize(data, msg)) {

            m_mqtt_abstraction.publish(msg.replyTo, serialize(everest_version));

            return true;
        }
        EVLOG_warning << "Failed to deserialize get_everest_version request.";
        return false;
    });
}

void LifecycleAPI::generate_api_var_status() {
    using SrcT = ::Everest::config::ActiveSlotStatus;
    using TarT = ::everest::lib::API::V1_0::types::lifecycle::ModuleExecutionStatusEnum;
    // indicate on the API that EVerest is alive
    publish_execution_status(Everest::Date::to_rfc3339(date::utc_clock::now()), TarT::NotRunning);

    // setup updates
    m_config_service.register_active_slot_update_handler([this](const Everest::config::ActiveSlotUpdate& update) {
        TarT module_status;

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
        case SrcT::RestartTriggered:
            module_status = TarT::RestartTriggered;
            break;
        default:
            // don't publish for other types of updates
            return;
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
    static const auto topic = m_topics.nonmodule_to_extern("status");
    static const auto cfg_api_availability = to_configuration_api_availability(m_config_api_availability);

    API_types_ext::ExecutionStatusUpdateNotice exec_status_update{};
    exec_status_update.tstamp = tstamp;
    exec_status_update.everest_running = true;
    exec_status_update.configuration_api_available = cfg_api_availability;
    exec_status_update.lifecycle_api_ro = m_readonly;

    exec_status_update.module_status = module_status;

    m_mqtt_abstraction.publish(topic, serialize(exec_status_update), QOS::QOS2, true);
}

void LifecycleAPI::subscribe_api_topic(std::string const& var, ParseAndPublishFtor const& parse_and_publish) {
    auto topic = m_topics.extern_to_nonmodule(var);
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
}
} // namespace Everest::api::lifecycle
