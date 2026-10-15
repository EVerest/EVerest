// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "VarContainer.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string_view>
#include <thread>

#include <utils/error/error_factory.hpp>
#include <utils/error/error_manager_impl.hpp>
#include <utils/error/error_state_monitor.hpp>

namespace everest::lib::API {

class CommCheckHandlerBase {
public:
    using HeartBeatFtor = std::function<bool()>;

    CommCheckHandlerBase(std::string_view error_type_, std::string_view default_sub_type_,
                         std::shared_ptr<Everest::error::ErrorStateMonitor>& error_state_monitor_,
                         std::shared_ptr<Everest::error::ErrorFactory>& error_factory_,
                         std::shared_ptr<Everest::error::ErrorManagerImpl>& error_manager_);
    ~CommCheckHandlerBase();

    bool check_error();
    void clear_error();
    void raise_error(std::string const& sub_type);
    void set_error();
    void set_value(bool value);
    void start(int timeout_s);
    bool heartbeat(int interval_ms, HeartBeatFtor const& ftor);

private:
    void communication_check_handler();

    std::chrono::seconds timeout;
    VarContainer<bool> comm_check_value;
    std::thread handler;
    std::thread heartbeat_handler;
    std::atomic_bool check_active{true};
    std::atomic_bool heartbeat_active{false};
    std::atomic_bool shutting_down{false};
    const std::string error_type;
    const std::string init_sub_type{"Initial communication check"};
    const std::string default_sub_type;
    std::shared_ptr<Everest::error::ErrorStateMonitor>& error_state_monitor;
    std::shared_ptr<Everest::error::ErrorFactory>& error_factory;
    std::shared_ptr<Everest::error::ErrorManagerImpl>& error_manager;
};

template <class InterfaceT> class CommCheckHandler : public CommCheckHandlerBase {
public:
    CommCheckHandler(std::string_view error_type_, std::string_view default_sub_type_,
                     const std::unique_ptr<InterfaceT>& interface) :
        CommCheckHandlerBase(error_type_, default_sub_type_, interface->error_state_monitor, interface->error_factory,
                             interface->error_manager) {
    }
};

} // namespace everest::lib::API
