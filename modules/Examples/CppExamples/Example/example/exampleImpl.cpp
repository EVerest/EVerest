// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "exampleImpl.hpp"

// initial cpp template for interface example_child
// this file should not be overwritten by the code generator again

namespace {
bool is_current_in_range(const double& value) {
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
namespace example {

void exampleImpl::init() {
    original_config = rw_config;
    mod->mqtt.subscribe("external/a",
                        [](json data) { EVLOG_error << "received data from external MQTT handler: " << data.dump(); });
}

void exampleImpl::ready() {
    publish_max_current(config.current);
    mod->r_kvs->call_store("test", "test");

    while (!shutdown_requested) {
        EVLOG_info << "Config log \"actual [original value]\": log_interval=" << std::to_string(mod->rw_config.log_interval)
                   << " [" << std::to_string(mod->original_config.log_interval) << "]; enum_test=\""
                   << mod->rw_config.enum_test << "\" [\"" << mod->original_config.enum_test
                   << "\"]; example|current=" << std::to_string(rw_config.current) << " ["
                   << std::to_string(original_config.current) << "]; example|enum_test=\"" << rw_config.enum_test
                   << "\" [\"" << original_config.enum_test << "\"]; example|enum_test2=\"" << rw_config.enum_test2
                   << "\" [\"" << original_config.enum_test2 << "\"]";
        std::unique_lock<std::mutex> lock(shutdown_mutex);
        shutdown_cv.wait_for(lock, std::chrono::seconds(mod->rw_config.log_interval),
                             [this]() { return shutdown_requested.load(); });
    }
    ready_finished = true;
    shutdown_cv.notify_one();
}

void exampleImpl::shutdown() {
    EVLOG_info << "Shutdown command received via framework, exiting process.";
    shutdown_requested = true;
    shutdown_cv.notify_one();

    // Wait for ready() to finish before exiting (with timeout as safety fallback)
    std::unique_lock<std::mutex> lock(shutdown_mutex);
    shutdown_cv.wait_for(lock, std::chrono::milliseconds(500), [this]() { return ready_finished.load(); });
}

bool exampleImpl::handle_uses_something(std::string& key) {
    if (mod->r_kvs->call_exists(key)) {
        EVLOG_debug << "IT SHOULD NOT AND DOES NOT EXIST";
    }

    Array test_array = {1, 2, 3};
    mod->r_kvs->call_store(key, test_array);

    bool exi = mod->r_kvs->call_exists(key);

    if (exi) {
        EVLOG_debug << "IT ACTUALLY EXISTS";
    }

    auto ret = mod->r_kvs->call_load(key);

    Array arr = std::get<Array>(ret);

    EVLOG_debug << "loaded array: " << arr << ", original array: " << test_array;

    return exi;
};

Everest::config::ConfigChangeResult exampleImpl::on_current_changed(const double& new_current) {
    const std::string log_prefix = "Cfg Update for 'current' | ";
    std::string desc = "rejected, as out-of-range";
    Everest::config::ConfigChangeResult ret =
        Everest::config::ConfigChangeResult::Rejected("New value is out of range.");

    if (is_current_in_range(new_current)) {
        rw_config.current = new_current;
        desc = "accepted";
        ret = Everest::config::ConfigChangeResult::Accepted();
    }

    EVLOG_info << log_prefix << "old == '" << std::to_string(rw_config.current) << "', new == '"
               << std::to_string(new_current) << "' " << desc;
    return ret;
}

Everest::config::ConfigChangeResult exampleImpl::on_enum_test_changed(const std::string& new_value) {
    const std::string log_prefix = "Cfg Update for 'enum_test' | ";
    std::string desc = "rejected, as invalid enum value";
    Everest::config::ConfigChangeResult ret =
        Everest::config::ConfigChangeResult::Rejected("New value is invalid enum value.");
    const std::string& old_value = rw_config.enum_test;

    if (is_valid_enum_test(new_value)) {
        if (new_value == old_value) {
            desc = "accepted, as no-change";
            ret = Everest::config::ConfigChangeResult::Accepted();
        } else if ((new_value == "one" and old_value != "one") or (new_value == "two" and old_value == "three")) {
            desc = "accepted, but requires reboot";
            ret = Everest::config::ConfigChangeResult::AcceptedRebootRequired();
        } else {
            desc = "accepted";
            rw_config.enum_test = new_value;
            ret = Everest::config::ConfigChangeResult::Accepted();
        }
    }

    EVLOG_info << log_prefix << "old == '" << rw_config.enum_test << "', new == '" << new_value << "' " << desc;
    return ret;
}

Everest::config::ConfigChangeResult exampleImpl::on_enum_test2_changed(const int& new_value) {
    const std::string log_prefix = "Cfg Update for 'enum_test2' | ";
    std::string desc = "rejected, as always-rejects";
    Everest::config::ConfigChangeResult ret = Everest::config::ConfigChangeResult::Rejected("Is a always rejected.");
    EVLOG_info << log_prefix << "old == '" << std::to_string(rw_config.enum_test2) << "', new == '"
               << std::to_string(new_value) << "' " << desc;
    return ret;
}

} // namespace example
} // namespace module
