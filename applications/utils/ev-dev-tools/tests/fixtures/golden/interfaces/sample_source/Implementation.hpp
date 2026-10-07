// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef SAMPLE_SOURCE_IMPLEMENTATION_HPP
#define SAMPLE_SOURCE_IMPLEMENTATION_HPP

//
// AUTO GENERATED - DO NOT EDIT!
// template version 6
//

#include <framework/ModuleAdapter.hpp>
#include <utils/types.hpp>
#include <utils/error.hpp>
#include <utils/error/error_state_monitor.hpp>
#include <utils/error/error_manager_impl.hpp>
#include <utils/error/error_factory.hpp>

#include "Types.hpp"

class sample_sourceImplBase : public Everest::ImplementationBase {
public:
    sample_sourceImplBase(Everest::ModuleAdapter* ev, const std::string& name)
        : Everest::ImplementationBase(),
        _ev(ev),
        _name(name) {
        if (ev == nullptr) {
            EVLOG_error << "ev is nullptr, please check the initialization of the module";
            error_manager = nullptr;
            error_state_monitor = nullptr;
            error_factory = nullptr;
            EVLOG_error << "error_manager, error_state_monitor and error_factory are nullptr";
        } else {
            error_manager = ev->get_error_manager_impl(name);
            if (error_manager == nullptr) {
                EVLOG_error << "error_manager is nullptr";
            }
            error_state_monitor = ev->get_error_state_monitor_impl(name);
            if (error_state_monitor == nullptr) {
                EVLOG_error << "error_state_monitor is nullptr";
            }
            error_factory = ev->get_error_factory(name);
            if (error_factory == nullptr) {
                EVLOG_error << "error_factory is nullptr";
            }
            impl_mapping = Everest::get_impl_mapping(ev->get_mapping(), name);
        }
    }

    // publish functions for variables
    void publish_latest(const types::sample::Reading& value) {
        _ev->publish(_name, "latest", value);
    }
    void publish_nothing_happened(const std::nullptr_t& value) {
        _ev->publish(_name, "nothing_happened", value);
    }
    void publish_moods_seen(const std::vector<types::sample::Mood>& value) {
        std::vector<std::string> string_array;
        string_array.reserve(value.size());
        for (const auto& entry : value) {
            string_array.push_back(types::sample::mood_to_string(entry));
        }
        _ev->publish(_name, "moods_seen", string_array);
    }
    void publish_count(const int& value) {
        _ev->publish(_name, "count", value);
    }

    void raise_error(const Everest::error::Error& error) {
        error_manager->raise_error(error);
    }

    void clear_error(const Everest::error::ErrorType& type) {
        error_manager->clear_error(type);
    }

    void clear_error(const Everest::error::ErrorType& type, const Everest::error::ErrorSubType& sub_type) {
        error_manager->clear_error(type, sub_type);
    }

    void clear_all_errors_of_impl() {
        error_manager->clear_all_errors();
    }

    void clear_all_errors_of_impl(const Everest::error::ErrorType& type) {
        error_manager->clear_all_errors(type);
    }

    std::shared_ptr<Everest::error::ErrorStateMonitor> error_state_monitor;
    std::shared_ptr<Everest::error::ErrorFactory> error_factory;
    std::shared_ptr<Everest::error::ErrorManagerImpl> error_manager;

    std::optional<Mapping> get_mapping() {
        return impl_mapping;
    }

protected:
    // command handler functions (virtual)
    virtual types::sample::Reading handle_configure(types::sample::Mood& mood, double& interval, bool& enabled, std::vector<types::sample::Mood>& allowed) = 0;
    virtual void handle_reset() = 0;
    virtual bool handle_store(std::variant<std::nullptr_t, Array, Object, bool, double, int, std::string>& value) = 0;

private:
    Everest::ModuleAdapter* const _ev;
    const std::string _name;
    std::optional<Mapping> impl_mapping;

    // helper function for getting all commands
    void _gather_cmds([[maybe_unused]] std::vector<Everest::cmd>& cmds) override {
        // configure command
        Everest::cmd configure_cmd;
        configure_cmd.impl_id = _name;
        configure_cmd.cmd_name = "configure";
        configure_cmd.arg_types = {
            {"mood", {"string"}},
            {"interval", {"number"}},
            {"enabled", {"boolean"}},
            {"allowed", {"array"}}
        };
        configure_cmd.cmd = [this](const Parameters& args) -> Result {
            auto mood = types::sample::string_to_mood(args.at("mood").get<std::string>());
            auto interval = static_cast<double>(args.at("interval"));
            auto enabled = static_cast<bool>(args.at("enabled"));
            json allowed_json_array = args.at("allowed");
            std::vector<types::sample::Mood> allowed;
            for (auto entry : allowed_json_array) {
                allowed.push_back(types::sample::string_to_mood(entry));
            }

            auto result = this->handle_configure(mood, interval, enabled, allowed);
            return result;
        };
        configure_cmd.return_type = {"object"};
        cmds.emplace_back(std::move(configure_cmd));

        // reset command
        Everest::cmd reset_cmd;
        reset_cmd.impl_id = _name;
        reset_cmd.cmd_name = "reset";
        // cmd reset has no arguments
        reset_cmd.cmd = [this](const Parameters& args) -> Result {
            (void) args; // no arguments used for this callback

            this->handle_reset();
            return nullptr;
        };
        cmds.emplace_back(std::move(reset_cmd));

        // store command
        Everest::cmd store_cmd;
        store_cmd.impl_id = _name;
        store_cmd.cmd_name = "store";
        store_cmd.arg_types = {
            {"value", {"null", "string", "number", "integer", "boolean", "array", "object"}}
        };
        store_cmd.cmd = [this](const Parameters& args) -> Result {
            auto value = Everest::json_to_variant<std::nullptr_t, Array, Object, bool, double, int, std::string>(args.at("value"));

            auto result = this->handle_store(value);
            return result;
        };
        store_cmd.return_type = {"boolean"};
        cmds.emplace_back(std::move(store_cmd));
    };
};

#endif // SAMPLE_SOURCE_IMPLEMENTATION_HPP
