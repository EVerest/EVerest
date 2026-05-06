// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#include "over_voltage_monitor_API.hpp"

#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/generic/string.hpp>
#include <everest_api_types/over_voltage_monitor/API.hpp>
#include <everest_api_types/over_voltage_monitor/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>

namespace module {

namespace API_generic = API_types::generic;
namespace API_ovm = API_types::over_voltage_monitor;
using ev_API::deserialize;

namespace {
Everest::error::Severity toInternalSeverity(API_ovm::ErrorSeverityEnum severity) {
    switch (severity) {
    case API_ovm::ErrorSeverityEnum::Low:
        return Everest::error::Severity::Low;
    case API_ovm::ErrorSeverityEnum::Medium:
        return Everest::error::Severity::Medium;
    case API_ovm::ErrorSeverityEnum::High:
        return Everest::error::Severity::High;
    default:
        return Everest::error::Severity::High;
    }
}
} // namespace

void over_voltage_monitor_API::init() {
    invoke_init(*p_main);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void over_voltage_monitor_API::ready() {
    invoke_ready(*p_main);

    generate_api_var_voltage_measurement_V();

    generate_api_var_raise_error();
    generate_api_var_clear_error();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

void over_voltage_monitor_API::generate_api_var_voltage_measurement_V() {
    helper.subscribe_api_topic("voltage_measurement_V", [this](std::string const& data) {
        float val = 0.0;
        if (deserialize(data, val)) {
            p_main->publish_voltage_measurement_V(val);
            return true;
        }
        return false;
    });
}

std::string over_voltage_monitor_API::make_error_string(API_types_ext::Error const& error) {
    auto error_str = API_generic::trimmed(serialize(error.type));
    auto result = "over_voltage_monitor/" + error_str;
    return result;
}

void over_voltage_monitor_API::generate_api_var_raise_error() {
    helper.subscribe_api_topic("raise_error", [=](const std::string& data) {
        API_types_ext::Error error;
        if (deserialize(data, error)) {
            auto sub_type_str = error.sub_type ? error.sub_type.value() : "";
            auto message_str = error.message ? error.message.value() : "";
            auto error_str = make_error_string(error);
            auto severity = error.severity.value_or(API_ovm::ErrorSeverityEnum::High);
            auto ev_error =
                p_main->error_factory->create_error(error_str, sub_type_str, message_str, toInternalSeverity(severity));
            p_main->raise_error(ev_error);
            return true;
        }
        return false;
    });
}

void over_voltage_monitor_API::generate_api_var_clear_error() {
    helper.subscribe_api_topic("clear_error", [=](const std::string& data) {
        API_types_ext::Error error;
        if (deserialize(data, error)) {
            std::string error_str = make_error_string(error);
            if (error.sub_type) {
                p_main->clear_error(error_str, error.sub_type.value());
            } else {
                p_main->clear_error(error_str);
            }
            return true;
        }
        return false;
    });
}

} // namespace module
