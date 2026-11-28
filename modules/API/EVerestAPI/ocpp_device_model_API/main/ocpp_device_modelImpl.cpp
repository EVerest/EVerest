// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ocpp_device_modelImpl.hpp"

#include <everest_api_types/ocpp/API.hpp>
#include <everest_api_types/ocpp/codec.hpp>
#include <everest_api_types/ocpp/json_codec.hpp>
#include <everest_api_types/ocpp/wrapper.hpp>
#include <everest_api_types/ocpp_device_model/API.hpp>
#include <everest_api_types/ocpp_device_model/codec.hpp>
#include <everest_api_types/ocpp_device_model/json_codec.hpp>
#include <everest_api_types/ocpp_device_model/wrapper.hpp>
#include <everest_api_types/utilities/AsyncApiRequestReply.hpp>

#include <everest/logging.hpp>

#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace API_types_ext = ev_API::V1_0::types::ocpp_device_model;

namespace module {
namespace main {

namespace {

std::string join(std::vector<std::string> const& names) {
    std::string result;
    for (auto const& name : names) {
        result += (result.empty() ? "" : ", ") + name;
    }
    return result;
}

} // namespace

void ocpp_device_modelImpl::init() {
    timeout_s = mod->config.cfg_request_reply_to_s;
}

void ocpp_device_modelImpl::ready() {
}

void ocpp_device_modelImpl::shutdown() {
}

types::ocpp_device_model::Registrations ocpp_device_modelImpl::handle_get_registrations() {
    mod->collector->start_collecting();
    API_types_ext::GetRegistrations prompt;
    prompt.expected_owners = mod->collector->expected_owners();
    mod->mqtt_v.publish(mod->helper.get_topics().everest_to_extern("get_registrations"),
                        API_types_ext::serialize(prompt));

    const auto answer = mod->collector->collect();
    if (not answer.missing_owners.empty()) {
        EVLOG_warning << "Owners missing after " << mod->config.cfg_expected_owners_timeout_s
                      << " s, answering without them: " << join(answer.missing_owners);
    }
    if (not answer.unexpected_owners.empty()) {
        EVLOG_warning << "Registered owners not in cfg_expected_owners: " << join(answer.unexpected_owners);
    }

    types::ocpp_device_model::Registrations result;
    for (auto const& registration : answer.registrations) {
        result.registrations.push_back(API_types_ext::to_internal_api(registration));
    }
    EVLOG_info << "Answering get_registrations with owners: " << (answer.owners.empty() ? "none" : join(answer.owners));
    return result;
}

void ocpp_device_modelImpl::handle_set_registration_result(types::ocpp_device_model::RegistrationResult& result) {
    mod->mqtt_v.publish(mod->helper.get_topics().everest_to_extern("set_registration_result"),
                        API_types_ext::serialize(API_types_ext::to_external_api(result)));
}

void ocpp_device_modelImpl::publish_write_result(const types::ocpp_device_model::WriteRequest& request,
                                                 const std::string& request_id,
                                                 const types::ocpp::SetVariableStatusEnumType status) {
    types::ocpp_device_model::WriteResult result;
    result.request_id = request_id;
    result.request = request;
    result.status = status;
    types::ocpp_device_model::OwnerMessage message;
    message.write_result = result;
    publish_owner_message(message);
}

types::ocpp::SetVariableStatusEnumType
ocpp_device_modelImpl::handle_set_value(types::ocpp_device_model::WriteRequest& request, std::string& request_id) {
    using ReplyT = ev_API::V1_0::types::ocpp::SetVariableStatusEnumType;
    struct Exchange {
        std::mutex mutex;
        std::condition_variable answered;
        std::optional<types::ocpp::SetVariableStatusEnumType> status;
        bool expired{false};
    };
    auto exchange = std::make_shared<Exchange>();

    // The reply is handled on the thread that handles every message of the owners, which publishes the write result
    // in its place among the owners' other messages
    static thread_local boost::uuids::random_generator uuid_create;
    const auto reply_topic = mod->helper.get_topics().reply_to_everest(boost::uuids::to_string(uuid_create()));
    auto unsubscribe =
        mod->mqtt_v.subscribe(reply_topic, [this, exchange, request, request_id](const std::string& raw) {
            auto status = types::ocpp::SetVariableStatusEnumType::Rejected;
            try {
                status = ev_API::V1_0::types::ocpp::to_internal_api(json::parse(raw).get<ReplyT>());
            } catch (const std::exception& e) {
                EVLOG_warning << "Reply to the write of " << request.component.name << "." << request.variable.name
                              << " does not parse, answering Rejected: " << e.what();
            }
            std::lock_guard<std::mutex> lock(exchange->mutex);
            if (exchange->status.has_value()) {
                EVLOG_info << "Ignoring a further reply to the write of " << request.component.name << "."
                           << request.variable.name;
                return;
            }
            if (exchange->expired) {
                EVLOG_info << "Ignoring a reply to the write of " << request.component.name << "."
                           << request.variable.name << " after the deadline";
                return;
            }
            publish_write_result(request, request_id, status);
            exchange->status = status;
            exchange->answered.notify_all();
        });

    json payload;
    payload["headers"]["replyTo"] = reply_topic;
    payload["payload"] = API_types_ext::to_external_api(request);
    mod->mqtt_v.publish(mod->helper.get_topics().everest_to_extern("set_value"), payload.dump());

    auto status = types::ocpp::SetVariableStatusEnumType::Rejected;
    {
        std::unique_lock<std::mutex> lock(exchange->mutex);
        if (exchange->answered.wait_for(lock, std::chrono::seconds(timeout_s),
                                        [&exchange]() { return exchange->status.has_value(); })) {
            status = exchange->status.value();
        } else {
            EVLOG_warning << "No reply to the write of " << request.component.name << "." << request.variable.name
                          << " within " << timeout_s << " s, answering Rejected";
            exchange->expired = true;
            publish_write_result(request, request_id, status);
        }
    }
    unsubscribe();
    return status;
}

} // namespace main
} // namespace module
