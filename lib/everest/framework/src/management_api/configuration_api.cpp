// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <configuration_api.hpp>
#include <everest/logging.hpp>
#include <utils/date.hpp>

#include <algorithm>
#include <iterator>

#include <api_request_handler.hpp>
#include <configuration_type_wrapper.hpp>
#include <everest_api_types/configuration/API.hpp>
#include <everest_api_types/configuration/codec.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>

namespace {
template <class SrcT, class ConvT> auto srcToTarVec(std::vector<SrcT> const& src, ConvT const& converter) {
    using TarT = decltype(converter(src[0]));

    std::vector<TarT> result;
    result.reserve(src.size());

    std::transform(src.begin(), src.end(), std::back_inserter(result), converter);

    return result;
}
} // namespace

namespace Everest::api::configuration {

namespace API_types = ev_API::V1_0::types;
namespace API_types_ext = API_types::configuration;
namespace API_generic = API_types::generic;
namespace API_wrapper = ::Everest::api::types::configuration;
using ev_API::deserialize;

ConfigurationAPI::ConfigurationAPI(MQTTAbstraction& mqtt_abstraction,
                                   Everest::config::ConfigServiceInterface& config_service, bool readonly) :
    m_mqtt_abstraction(mqtt_abstraction), m_config_service(config_service), m_readonly(readonly) {
    m_topics.setup("_unused_", "configuration", 1);

    generate_api_cmd_list_all_slots();
    generate_api_cmd_get_active_slot();
    generate_api_cmd_mark_active_slot();

    generate_api_cmd_delete_slot();
    generate_api_cmd_duplicate_slot();
    generate_api_cmd_load_from_yaml();
    generate_api_cmd_set_description();
    generate_api_cmd_set_config_parameters();
    generate_api_cmd_get_config_parameters();
    generate_api_cmd_get_configuration();

    generate_api_var_active_slot();
    generate_api_var_config_updates();
}

ConfigurationAPI::~ConfigurationAPI() {
    // The manager stops the MQTT message-handler worker threads before destroying this object, so no
    // handler can be running here. Unsubscribe every topic we registered.
    for (const auto& [topic, token] : m_registered_handlers) {
        m_mqtt_abstraction.unregister_handler(topic, token);
    }
}

void ConfigurationAPI::generate_api_cmd_list_all_slots() {
    subscribe_api_topic("list_all_slots", [this](std::string const& data) {
        // ListSlotIdsResult has no failure shape; an empty list is the closest spec-legal reply
        // for the exception path so a waiting client does not hang.
        return handle_request(m_mqtt_abstraction, data, "list_all_slots", API_types_ext::ListSlotIdsResult{},
                              [this](API_generic::RequestReply const& msg) {
                                  auto all_slots = m_config_service.list_all_slots();
                                  publish_reply(m_mqtt_abstraction, msg, API_wrapper::to_external_api(all_slots));
                                  return true;
                              });
    });
}

void ConfigurationAPI::generate_api_cmd_get_active_slot() {
    subscribe_api_topic("get_active_slot", [this](std::string const& data) {
        // GetActiveSlotIdResult has no failure shape; an empty result is the closest spec-legal
        // reply for the exception path so a waiting client does not hang.
        return handle_request(m_mqtt_abstraction, data, "get_active_slot", API_types_ext::GetActiveSlotIdResult{},
                              [this](API_generic::RequestReply const& msg) {
                                  API_types_ext::GetActiveSlotIdResult payload;
                                  payload.active_slot_id = m_config_service.get_active_slot_id();
                                  payload.next_boot_slot_id = m_config_service.get_next_boot_slot_id();
                                  publish_reply(m_mqtt_abstraction, msg, payload);
                                  return true;
                              });
    });
}

void ConfigurationAPI::generate_api_cmd_mark_active_slot() {
    subscribe_api_topic("mark_active_slot", [this](std::string const& data) {
        return handle_request(
            m_mqtt_abstraction, data, "mark_active_slot",
            API_types_ext::MarkActiveSlotResult{API_types_ext::MarkActiveSlotResultEnum::Failed},
            [this](API_generic::RequestReply const& msg) {
                API_types_ext::MarkActiveSlotRequest payload;
                if (not deserialize(msg.payload, payload)) {
                    return false;
                }
                if (m_readonly) {
                    publish_reply(
                        m_mqtt_abstraction, msg,
                        API_types_ext::MarkActiveSlotResult{API_types_ext::MarkActiveSlotResultEnum::AccessDenied});
                } else {
                    auto int_res = m_config_service.mark_active_slot(payload.slot_id);
                    publish_reply(m_mqtt_abstraction, msg,
                                  API_types_ext::MarkActiveSlotResult{API_wrapper::to_external_api(int_res)});
                }
                return true;
            });
    });
}

void ConfigurationAPI::generate_api_cmd_delete_slot() {
    subscribe_api_topic("delete_slot", [this](std::string const& data) {
        return handle_request(
            m_mqtt_abstraction, data, "delete_slot",
            API_types_ext::DeleteSlotResult{API_types_ext::DeleteSlotResultEnum::Failed},
            [this](API_generic::RequestReply const& msg) {
                API_types_ext::DeleteSlotRequest payload;
                if (not deserialize(msg.payload, payload)) {
                    return false;
                }
                if (m_readonly) {
                    publish_reply(m_mqtt_abstraction, msg,
                                  API_types_ext::DeleteSlotResult{API_types_ext::DeleteSlotResultEnum::AccessDenied});
                } else {
                    auto int_res = m_config_service.delete_slot(payload.slot_id);
                    publish_reply(m_mqtt_abstraction, msg,
                                  API_types_ext::DeleteSlotResult{API_wrapper::to_external_api(int_res)});
                }
                return true;
            });
    });
}

void ConfigurationAPI::generate_api_cmd_duplicate_slot() {
    subscribe_api_topic("duplicate_slot", [this](std::string const& data) {
        return handle_request(
            m_mqtt_abstraction, data, "duplicate_slot", API_types_ext::DuplicateSlotResult{false, std::nullopt},
            [this](API_generic::RequestReply const& msg) {
                API_types_ext::DuplicateSlotRequest payload;
                if (not deserialize(msg.payload, payload)) {
                    return false;
                }
                if (m_readonly) {
                    publish_reply(m_mqtt_abstraction, msg, API_types_ext::DuplicateSlotResult{false, std::nullopt});
                } else {
                    auto res = m_config_service.duplicate_slot(payload.slot_id, payload.new_description);
                    publish_reply(m_mqtt_abstraction, msg, API_wrapper::to_external_api(res));
                }
                return true;
            });
    });
}

void ConfigurationAPI::generate_api_cmd_load_from_yaml() {
    subscribe_api_topic("load_from_yaml", [this](std::string const& data) {
        return handle_request(
            m_mqtt_abstraction, data, "load_from_yaml",
            API_types_ext::LoadFromYamlResult{false, "Request failed", std::nullopt},
            [this](API_generic::RequestReply const& msg) {
                API_types_ext::LoadFromYamlRequest payload;
                if (not deserialize(msg.payload, payload)) {
                    return false;
                }
                if (m_readonly) {
                    publish_reply(m_mqtt_abstraction, msg,
                                  API_types_ext::LoadFromYamlResult{false, "Not Allowed", std::nullopt});
                } else {
                    std::optional<int> slot_id_opt =
                        payload.slot_id.has_value() ? std::optional<int>{payload.slot_id.value()} : std::nullopt;
                    auto res = m_config_service.load_from_yaml(payload.raw_yaml, payload.description, slot_id_opt);
                    publish_reply(m_mqtt_abstraction, msg, API_wrapper::to_external_api(res));
                }
                return true;
            });
    });
}

void ConfigurationAPI::generate_api_cmd_set_description() {
    subscribe_api_topic("set_description", [this](std::string const& data) {
        return handle_request(
            m_mqtt_abstraction, data, "set_description", API_types_ext::SetDescriptionResult{false},
            [this](API_generic::RequestReply const& msg) {
                API_types_ext::SetDescriptionRequest payload;
                if (not deserialize(msg.payload, payload)) {
                    return false;
                }
                if (m_readonly) {
                    publish_reply(m_mqtt_abstraction, msg, API_types_ext::SetDescriptionResult{false});
                } else {
                    bool success = m_config_service.set_description(payload.slot_id, payload.description);

                    API_types_ext::SetDescriptionResult ext_res;
                    ext_res.success = success;

                    publish_reply(m_mqtt_abstraction, msg, ext_res);
                }
                return true;
            });
    });
}

void ConfigurationAPI::generate_api_cmd_set_config_parameters() {
    subscribe_api_topic("set_config_parameters", [this](std::string const& data) {
        // ConfigurationParameterUpdateResultEnum is per-parameter, so a request-level failure with
        // an unknown update count (invalid payload / exception) can only reply with an empty result
        // list. When the update count is known (readonly, internal failure below) each update is
        // reported Rejected instead.
        return handle_request(
            m_mqtt_abstraction, data, "set_config_parameters",
            API_types_ext::ConfigurationParameterUpdateRequestResult{}, [this](API_generic::RequestReply const& msg) {
                API_types_ext::ConfigurationParameterUpdateRequest payload;
                if (not deserialize(msg.payload, payload)) {
                    return false;
                }
                if (m_readonly) {
                    API_types_ext::ConfigurationParameterUpdateRequestResult response;

                    std::fill_n(std::back_inserter(response.results), payload.parameter_updates.size(),
                                API_types_ext::ConfigurationParameterUpdateResultEnum::Rejected);

                    publish_reply(m_mqtt_abstraction, msg, response);
                } else {
                    std::vector<Everest::config::ConfigParameterUpdate> updates_internal = srcToTarVec(
                        payload.parameter_updates, [](const API_types_ext::ConfigurationParameterUpdate& update_ext) {
                            return API_wrapper::to_internal_api(update_ext);
                        });

                    auto int_res =
                        m_config_service.set_config_parameters(payload.slot_id, updates_internal, {true, std::nullopt});

                    API_types_ext::ConfigurationParameterUpdateRequestResult response{};
                    if (int_res.parameter_results.has_value()) {
                        response.results = srcToTarVec(int_res.parameter_results.value(), [](const auto& result) {
                            return API_wrapper::to_external_api(result.status);
                        });
                        if (int_res.status == Everest::config::SetConfigParameterStatus::ModulesInTransientState) {
                            EVLOG_warning << "Cannot process set_config_parameters request on the active slot while "
                                             "modules are in a transient state.";
                        }
                    } else {
                        // Internal config service failed: reply Rejected for every requested update so
                        // the client sees a per-parameter failure instead of hanging.
                        EVLOG_warning << "Internal config service failed to process the set_config_parameters request.";
                        std::fill_n(std::back_inserter(response.results), payload.parameter_updates.size(),
                                    API_types_ext::ConfigurationParameterUpdateResultEnum::Rejected);
                    }
                    publish_reply(m_mqtt_abstraction, msg, response);
                }
                return true;
            });
    });
}

void ConfigurationAPI::generate_api_cmd_get_config_parameters() {
    subscribe_api_topic("get_config_parameters", [this](std::string const& data) {
        return handle_request(
            m_mqtt_abstraction, data, "get_config_parameters",
            API_types_ext::GetConfigurationParameterResult{API_types_ext::GetConfigurationStatusEnum::Failed,
                                                           std::nullopt},
            [this](API_generic::RequestReply const& msg) {
                API_types_ext::GetConfigurationParameterRequest payload;
                if (not deserialize(msg.payload, payload)) {
                    return false;
                }
                bool force_read_from_db = payload.force_read_from_db.value_or(false);
                std::vector<everest::config::ConfigurationParameterIdentifier> requested_parameters = srcToTarVec(
                    payload.parameters, [](const API_types_ext::ConfigurationParameterIdentifier& parameter_ext) {
                        return API_wrapper::to_internal_api(parameter_ext);
                    });

                auto int_res =
                    m_config_service.get_config_parameters(payload.slot_id, requested_parameters, force_read_from_db);

                API_types_ext::GetConfigurationParameterResult response{};
                response.status = API_wrapper::to_external_api(int_res.status);
                if (int_res.status == Everest::config::GetConfigurationStatus::Success) {
                    response.parameter_values = srcToTarVec(
                        int_res.parameters, [](const auto& result) { return API_wrapper::to_external_api(result); });
                }
                publish_reply(m_mqtt_abstraction, msg, response);
                return true;
            });
    });
}

void ConfigurationAPI::generate_api_cmd_get_configuration() {
    subscribe_api_topic("get_configuration", [this](std::string const& data) {
        return handle_request(
            m_mqtt_abstraction, data, "get_configuration",
            API_types_ext::GetConfigurationResult{API_types_ext::GetConfigurationStatusEnum::Failed, std::nullopt},
            [this](API_generic::RequestReply const& msg) {
                API_types_ext::GetConfigurationRequest payload;
                if (not deserialize(msg.payload, payload)) {
                    return false;
                }
                bool force_read_from_db = payload.force_read_from_db.value_or(false);
                auto res = m_config_service.get_configuration(payload.slot_id, force_read_from_db);
                publish_reply(m_mqtt_abstraction, msg, API_wrapper::to_external_api(res));
                return true;
            });
    });
}

void ConfigurationAPI::generate_api_var_active_slot() {
    API_types_ext::ActiveSlotUpdateNotice initial_update{};
    initial_update.tstamp = Everest::Date::to_rfc3339(date::utc_clock::now());
    initial_update.active_slot_id = m_config_service.get_active_slot_id();
    initial_update.next_boot_slot_id = m_config_service.get_next_boot_slot_id();
    // set this to Stopped, as the API is created before any module starts
    initial_update.status = API_types_ext::ActiveSlotStatusEnum::Stopped;
    m_mqtt_abstraction.publish(m_topics.nonmodule_to_extern("active_slot"), serialize(initial_update));

    m_config_service.register_active_slot_update_handler([this](const Everest::config::ActiveSlotUpdate& update) {
        auto ext_update = API_wrapper::to_external_api(update);
        m_mqtt_abstraction.publish(m_topics.nonmodule_to_extern("active_slot"), serialize(ext_update));
    });
}

void ConfigurationAPI::generate_api_var_config_updates() {
    m_config_service.register_config_update_handler([this](const Everest::config::ConfigurationUpdate& update) {
        auto ext_update = API_wrapper::to_external_api(update);
        m_mqtt_abstraction.publish(m_topics.nonmodule_to_extern("config_updates"), serialize(ext_update));
    });
}

void ConfigurationAPI::subscribe_api_topic(std::string const& var, ParseAndPublishFtor const& parse_and_publish) {
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
    m_registered_handlers.emplace_back(topic, handler);
}
} // namespace Everest::api::configuration
