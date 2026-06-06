// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "include/lifecycle_api.hpp"
#include <everest/logging.hpp>
#include <utils/date.hpp>

#include <algorithm>
#include <iterator>

#include "include/lifecycle_type_wrapper.hpp"
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/lifecycle/API.hpp>
#include <everest_api_types/lifecycle/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>

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

void LifecycleAPI::modules_started_running() {
    module_runtime_status_changed(true);
}

void LifecycleAPI::modules_stopped_running() {
    module_runtime_status_changed(false);
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

void LifecycleAPI::module_runtime_status_changed(bool running) {
    static const auto topic = m_topics.nonmodule_to_extern("status");

    API_types_ext::ExecutionStatusUpdateNotice exec_status_update{};
    exec_status_update.configuration_api_available = to_configuration_api_availability(m_config_api_availability);
    exec_status_update.tstamp = Everest::Date::to_rfc3339(date::utc_clock::now());
    if (running) {
        exec_status_update.status = API_types_ext::ModuleExecutionStatusEnum::Running;
    } else {
        exec_status_update.status = API_types_ext::ModuleExecutionStatusEnum::NotRunning;
    }

    m_mqtt_abstraction.publish(topic, serialize(exec_status_update), QOS::QOS2, false);
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
