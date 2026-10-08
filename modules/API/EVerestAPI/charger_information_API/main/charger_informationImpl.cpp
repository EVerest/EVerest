// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "charger_informationImpl.hpp"

#include <everest_api_types/charger_information/API.hpp>
#include <everest_api_types/charger_information/codec.hpp>
#include <everest_api_types/charger_information/json_codec.hpp>
#include <everest_api_types/charger_information/wrapper.hpp>
#include <everest_api_types/utilities/AsyncApiRequestReply.hpp>

#include <generated/types/charger_information.hpp>

#include <utility>

#include <everest/logging.hpp>

namespace API_types_ext = ev_API::V1_0::types::charger_information;

namespace module {
namespace main {

void charger_informationImpl::init() {
    timeout_s = mod->config.cfg_request_reply_to_s;
}

void charger_informationImpl::ready() {
}

void charger_informationImpl::shutdown() {
}

types::charger_information::ChargerInformation charger_informationImpl::handle_get_charger_information() {
    auto result = ev_API::request_reply_handler<API_types_ext::ChargerInformation>(
        mod->mqtt_v, mod->helper.get_topics(), "get_charger_information", timeout_s);
    if (!result) {
        EVLOG_warning << "No reply to get_charger_information within " << timeout_s
                      << "s, answering with empty charger information";
        return {};
    }
    return std::move(*result);
}

} // namespace main
} // namespace module
