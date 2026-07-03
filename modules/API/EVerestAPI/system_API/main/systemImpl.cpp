// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "systemImpl.hpp"

#include <everest_api_types/system/API.hpp>
#include <everest_api_types/system/codec.hpp>
#include <everest_api_types/system/json_codec.hpp>
#include <everest_api_types/system/wrapper.hpp>
#include <everest_api_types/utilities/AsyncApiRequestReply.hpp>

#include <generated/types/system.hpp>

namespace API_types_ext = ev_API::V1_0::types::system;
namespace {
bool to_external_api(bool value) {
    return value;
}
} // namespace

namespace module {
namespace main {

void systemImpl::init() {
    timeout_s = mod->config.cfg_request_reply_to_s;
}

void systemImpl::ready() {
}

template <class T, class ReqT>
auto systemImpl::generic_request_reply(T const& default_value, ReqT const& request, std::string const& topic) {
    using namespace API_types_ext;
    using ExtT = decltype(to_external_api(std::declval<T>()));
    auto result = everest::lib::API::request_reply_handler<ExtT>(mod->mqtt_v, mod->helper.get_topics(), request, topic,
                                                                 timeout_s);
    if (!result) {
        return default_value;
    }
    return result.value();
}

types::system::UpdateFirmwareResponse
systemImpl::handle_update_firmware(types::system::FirmwareUpdateRequest& firmware_update_request) {
    static const types::system::UpdateFirmwareResponse default_response =
        types::system::UpdateFirmwareResponse::Rejected;
    return generic_request_reply(default_response, API_types_ext::to_external_api(firmware_update_request),
                                 "update_firmware");
}

void systemImpl::handle_allow_firmware_installation() {
    static const auto topic = mod->helper.get_topics().everest_to_extern("allow_firmware_installation");
    mod->mqtt_v.publish(topic, "");
}

types::system::UploadLogsResponse
systemImpl::handle_upload_logs(types::system::UploadLogsRequest& upload_logs_request) {
    static const types::system::UploadLogsResponse default_response =
        types::system::UploadLogsResponse{types::system::UploadLogsStatus::Rejected, {}};
    return generic_request_reply(default_response, API_types_ext::to_external_api(upload_logs_request), "upload_logs");
}

bool systemImpl::handle_is_reset_allowed(types::system::ResetType& type) {
    static const bool default_response = false;
    return generic_request_reply(default_response, API_types_ext::to_external_api(type), "is_reset_allowed");
}

void systemImpl::handle_reset(types::system::ResetType& type, bool& scheduled) {
    static const auto topic = mod->helper.get_topics().everest_to_extern("reset");
    json args = API_types_ext::ResetRequest{API_types_ext::to_external_api(type), scheduled};
    mod->mqtt_v.publish(topic, args.dump());
}

bool systemImpl::handle_set_system_time(std::string& timestamp) {
    static const bool default_response = false;
    return generic_request_reply(default_response, timestamp, "set_system_time");
}

types::system::BootReason systemImpl::handle_get_boot_reason() {
    static const types::system::BootReason default_respone = types::system::BootReason::Unknown;
    return generic_request_reply(default_respone, ev_API::internal::empty_payload, "get_boot_reason");
}

types::network::ConfigureNetworkResponse
systemImpl::handle_configure_network(types::network::ConfigureNetworkRequest& request) {
    types::network::ConfigureNetworkResponse response;
    response.status = types::network::ConfigureNetworkStatusEnum::NotSupported;
    return response;
}

} // namespace main
} // namespace module
