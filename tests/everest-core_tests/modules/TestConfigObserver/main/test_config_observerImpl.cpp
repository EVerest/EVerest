// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "test_config_observerImpl.hpp"

namespace module {
namespace main {

void test_config_observerImpl::init() {
    get_config_service_client()->register_config_update_handler(
        mod->config.observed_module_id, [this](const Everest::config::ConfigUpdateNotification& notification) {
            nlohmann::json entry = {
                {"module_id", notification.identifier.module_id},
                {"parameter", notification.identifier.configuration_parameter_name},
                {"value", notification.value},
                {"status", Everest::config::conversions::set_response_status_to_string(notification.status)},
            };
            if (notification.origin.has_value()) {
                entry["origin"] = notification.origin.value();
            }
            received.handle()->push_back(std::move(entry));
        });
}

void test_config_observerImpl::ready() {
}

void test_config_observerImpl::shutdown() {
}

Array test_config_observerImpl::handle_get_received() {
    auto handle = received.handle();
    return Array(handle->begin(), handle->end());
}

} // namespace main
} // namespace module
