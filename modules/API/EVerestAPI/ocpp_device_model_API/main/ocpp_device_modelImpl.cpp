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

types::ocpp::SetVariableStatusEnumType
ocpp_device_modelImpl::handle_set_value(types::ocpp_device_model::WriteRequest& request) {
    using ReplyT = ev_API::V1_0::types::ocpp::SetVariableStatusEnumType;
    auto result = ev_API::request_reply_handler<ReplyT>(
        mod->mqtt_v, mod->helper.get_topics(), API_types_ext::to_external_api(request), "set_value", timeout_s);
    if (not result) {
        return types::ocpp::SetVariableStatusEnumType::Rejected;
    }
    return result.value();
}

} // namespace main
} // namespace module
