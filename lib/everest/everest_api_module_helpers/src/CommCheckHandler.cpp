// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/logging.hpp>
#include <everest_api_module_helpers/CommCheckHandler.hpp>

#include <utility>

namespace everest::lib::API {

// ----------------------------------------------------------------------------
// public methods

CommCheckHandlerBase::CommCheckHandlerBase(std::string_view error_type_, std::string_view default_sub_type_,
                                           std::shared_ptr<Everest::error::ErrorStateMonitor>& error_state_monitor_,
                                           std::shared_ptr<Everest::error::ErrorFactory>& error_factory_,
                                           std::shared_ptr<Everest::error::ErrorManagerImpl>& error_manager_) :
    error_type(error_type_),
    default_sub_type(default_sub_type_),
    error_state_monitor(error_state_monitor_),
    error_factory(error_factory_),
    error_manager(error_manager_) {
}

void CommCheckHandlerBase::raise_error(std::string const& sub_type) {
    const std::string message{"Send communication_check to clear the error"};
    auto error = error_factory->create_error(error_type, sub_type, message, Everest::error::Severity::High);
    try {
        return error_manager->raise_error(error);
    } catch (...) {
    }
}

void CommCheckHandlerBase::clear_error() {
    try {
        if (error_state_monitor->is_error_active(error_type, default_sub_type)) {
            error_manager->clear_error(error_type, default_sub_type);
        }
        if (error_state_monitor->is_error_active(error_type, init_sub_type)) {
            error_manager->clear_error(error_type, init_sub_type);
        }
    } catch (...) {
        EVLOG_info << "Failed to clear error: " << error_type;
    }
}

bool CommCheckHandlerBase::check_error() {
    return error_state_monitor->is_error_active(error_type, default_sub_type) ||
           error_state_monitor->is_error_active(error_type, init_sub_type);
}

CommCheckHandlerBase::~CommCheckHandlerBase() {
    std::thread heartbeat_handler;
    {
        auto state_handle = state.handle();
        state_handle->shutting_down = true;
        heartbeat_handler = std::move(state_handle->heartbeat_handler);
    }
    state.notify_all();
    if (handler.joinable()) {
        handler.join();
    }
    if (heartbeat_handler.joinable()) {
        heartbeat_handler.join();
    }
}

void CommCheckHandlerBase::set_value(bool value) {
    state.handle()->comm_check_value = value;
    state.notify_all();
}

void CommCheckHandlerBase::set_error() {
    if (check_active.load()) {
        raise_error(default_sub_type);
    }
}

void CommCheckHandlerBase::start(int timeout_s) {
    timeout = std::chrono::seconds(timeout_s);
    if (timeout_s < 1) {
        EVLOG_info << "No communication checks" << std::endl;
        check_active.store(false);
    } else {
        handler = std::thread([this]() { this->communication_check_handler(); });
    }
}

bool CommCheckHandlerBase::heartbeat(int interval_ms, HeartBeatFtor const& ftor) {
    bool started = false;
    if (interval_ms > 0) {
        auto interval = std::chrono::milliseconds(interval_ms);
        auto action = [this, interval, ftor]() {
            auto state_handle = state.handle();
            while (not state_handle->shutting_down) {
                const auto next = std::chrono::steady_clock::now() + interval;
                state_handle.unlock();
                const auto result = ftor();
                state_handle.lock();
                if (not result) {
                    break;
                }
                state_handle.wait_until(next, [&state_handle] { return state_handle->shutting_down; });
            }
        };
        auto state_handle = state.handle();
        if (not state_handle->shutting_down and not state_handle->heartbeat_handler.joinable()) {
            state_handle->heartbeat_handler = std::thread(action);
            started = true;
        }
    }
    return started;
}

// ----------------------------------------------------------------------------
// private methods

void CommCheckHandlerBase::communication_check_handler() {
    auto timeout_sub_type = init_sub_type;
    auto state_handle = state.handle();
    while (check_active.load()) {
        state_handle.wait_for(
            [&state_handle] { return state_handle->shutting_down or state_handle->comm_check_value.has_value(); },
            timeout);
        if (state_handle->shutting_down) {
            break;
        }
        const auto check = std::exchange(state_handle->comm_check_value, std::nullopt);
        state_handle.unlock();
        if (check.has_value()) {
            timeout_sub_type = default_sub_type;
            check_active.store(check.value());
            if (check_error()) {
                EVLOG_info << "Communication check: SUCCESS " << std::endl;
                clear_error();
            }
        } else {
            if (!check_error()) {
                EVLOG_info << "Communication check: FAILURE " << std::endl;
                raise_error(timeout_sub_type);
            }
        }
        state_handle.lock();
    }
}

} // namespace everest::lib::API
