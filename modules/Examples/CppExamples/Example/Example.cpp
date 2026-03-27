// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "Example.hpp"

#include <chrono>
#include <thread>

namespace {
bool is_log_interval_in_range(const int& value) {
    return value >= 1 and value <= 60;
}

bool is_valid_enum_test(const std::string& value) {
    if (value == "one" or value == "two" or value == "three") {
        return true;
    }
    return false;
}
} // namespace

namespace module {

void Example::init() {
    original_config = rw_config;

    invoke_init(*p_example);
    invoke_init(*p_store);
}

void Example::ready() {
    invoke_ready(*p_example);
    invoke_ready(*p_store);

    mqtt.publish("external/topic", "data");
}

void Example::shutdown() {
    invoke_shutdown(*p_example);
    invoke_shutdown(*p_store);
}

Everest::config::ConfigChangeResult Example::on_log_interval_changed(const int& new_interval) {
    const std::string log_prefix = "Cfg Update for 'log_interval' | ";
    std::string desc = "rejected, as out-of-range";
    Everest::config::ConfigChangeResult ret =
        Everest::config::ConfigChangeResult::Rejected("New value is out of range.");

    if (is_log_interval_in_range(new_interval)) {
        if (new_interval < rw_config.log_interval) {
            rw_config.log_interval = new_interval;
            desc = "accepted";
            ret = Everest::config::ConfigChangeResult::Accepted();
        } else if (new_interval > rw_config.log_interval) {
            desc = "accepted for next reboot";
            ret = Everest::config::ConfigChangeResult::AcceptedRebootRequired();
        }
    }

    EVLOG_info << log_prefix << "old == '" << std::to_string(rw_config.log_interval) << "', new == '"
               << std::to_string(new_interval) << "' " << desc;
    return ret;
}

Everest::config::ConfigChangeResult Example::on_enum_test_changed(const std::string& new_value) {
    const std::string log_prefix = "Cfg Update for 'enum_test' | ";
    std::string desc = "rejected, as invalid enum value";
    Everest::config::ConfigChangeResult ret =
        Everest::config::ConfigChangeResult::Rejected("New value is invalid enum value.");

    if (is_valid_enum_test(new_value)) {
        rw_config.enum_test = new_value;
        ret = Everest::config::ConfigChangeResult::Accepted();
        desc = "accepted";
    }

    EVLOG_info << log_prefix << "old == '" << rw_config.enum_test << "', new == '" << new_value << "' " << desc;
    return ret;
}

} // namespace module
