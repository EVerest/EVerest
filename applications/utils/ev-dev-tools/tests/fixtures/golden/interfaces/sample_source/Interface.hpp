// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef SAMPLE_SOURCE_INTERFACE_HPP
#define SAMPLE_SOURCE_INTERFACE_HPP

//
// AUTO GENERATED - DO NOT EDIT!
// template version 6
//

#include <framework/ModuleAdapter.hpp>
#include <utils/types.hpp>
#include <utils/error.hpp>
#include <utils/error/error_state_monitor.hpp>
#include <utils/error/error_manager_req.hpp>

#include "Types.hpp"

class sample_sourceIntf {
public:
    sample_sourceIntf(Everest::ModuleAdapter* adapter, Requirement req, const std::string& module_id, std::optional<Mapping> mapping)
        : module_id(module_id),
        _adapter(adapter),
        _req(req),
        _mapping(mapping) {
        if (adapter == nullptr) {
            EVLOG_error << "adapter is nullptr, please check the initialization of the module";
            error_manager = nullptr;
            error_state_monitor = nullptr;
            EVLOG_error << "error_manager and error_state_monitor are nullptr";
        } else {
            error_manager = adapter->get_error_manager_req(req);
            if (error_manager == nullptr) {
                EVLOG_error << "error_manager is nullptr";
            }
            error_state_monitor = adapter->get_error_state_monitor_req(req);
            if (error_state_monitor == nullptr) {
                EVLOG_error << "error_state_monitor is nullptr";
            }
        }
    }

    const std::string module_id;

    // variables available for subscription
    void subscribe_latest(const std::function<void(const types::sample::Reading&)>& listener) {
        _adapter->subscribe(_req, "latest", [func = std::move(listener)](const Value& value) {
            func(value);
        });
    }

    void subscribe_nothing_happened(const std::function<void()>& listener) {
        _adapter->subscribe(_req, "nothing_happened", [func = std::move(listener)](const Value& value) {
            if (not Everest::detail::is_type_compatible<std::nullptr_t>(value.type())) {
                EVLOG_error << "Callback for variable 'nothing_happened' in interface 'sample_source' has wrong type!";
            }
            func();
        });
    }

    void subscribe_moods_seen(const std::function<void(const std::vector<types::sample::Mood>&)>& listener) {
        _adapter->subscribe(_req, "moods_seen", [func = std::move(listener)](const Value& value) {
            std::vector<types::sample::Mood> typed_value;
            for (auto& entry : value) {
                typed_value.push_back(types::sample::string_to_mood(entry));
            }
            func(typed_value);
        });
    }

    void subscribe_count(const std::function<void(const int&)>& listener) {
        _adapter->subscribe(_req, "count", [func = std::move(listener)](const Value& value) {
            func(static_cast<int>(value));
        });
    }

    void subscribe_error(
        const Everest::error::ErrorType& type,
        const Everest::error::ErrorCallback& callback,
        const Everest::error::ErrorCallback& clear_callback
    ) {
        error_manager->subscribe_error(type, callback, clear_callback);
    }

    void subscribe_all_errors(
        const Everest::error::ErrorCallback& callback,
        const Everest::error::ErrorCallback& clear_callback
    ) {
        error_manager->subscribe_all_errors(callback, clear_callback);
    }

    // commands available to call
    types::sample::Reading call_configure(const types::sample::Mood& mood, const double& interval, const bool& enabled, const std::vector<types::sample::Mood>& allowed) {
        Array allowed_array;
        for (const auto& allowed_entry : allowed) {
            allowed_array.push_back(types::sample::mood_to_string(allowed_entry));
        }
Result result = _adapter->call(_req, "configure", Parameters{
            {"mood", types::sample::mood_to_string(mood)},
            {"interval", interval},
            {"enabled", enabled},
            {"allowed", allowed_array}
        });
        json retval_json = result.value();
        types::sample::Reading retval = retval_json;
        return retval;
    }

    void call_reset() {
_adapter->call(_req, "reset", Parameters{
        });
    }

    bool call_store(std::variant<std::nullptr_t, Array, Object, bool, double, int, std::string> value) {
Result result = _adapter->call(_req, "store", Parameters{
            {"value", Everest::variant_to_json(value)}
        });
        auto retval = static_cast<bool>(result.value());
        return retval;
    }

    std::shared_ptr<Everest::error::ErrorStateMonitor> error_state_monitor;

    std::optional<Mapping> get_mapping() {
        return _mapping;
    }

private:
    std::shared_ptr<Everest::error::ErrorManagerReq> error_manager;
    Everest::ModuleAdapter* const _adapter;
    Requirement _req;
    std::optional<Mapping> _mapping;
};

#endif // SAMPLE_SOURCE_INTERFACE_HPP
