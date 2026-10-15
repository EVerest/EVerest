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
    {
        std::scoped_lock lock(state_mutex);
        shutting_down = true;
    }
    state_cv.notify_all();
    if (handler.joinable()) {
        handler.join();
    }
    if (heartbeat_handler.joinable()) {
        heartbeat_handler.join();
    }
}

void CommCheckHandlerBase::set_value(bool value) {
    {
        std::scoped_lock lock(state_mutex);
        comm_check_value = value;
    }
    state_cv.notify_all();
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
            std::unique_lock lock(state_mutex);
            while (not shutting_down) {
                const auto next = std::chrono::steady_clock::now() + interval;
                lock.unlock();
                const auto result = ftor();
                lock.lock();
                if (not result) {
                    break;
                }
                state_cv.wait_until(lock, next, [this] { return shutting_down; });
            }
        };
        std::scoped_lock lock(state_mutex);
        if (not shutting_down and not heartbeat_handler.joinable()) {
            heartbeat_handler = std::thread(action);
            started = true;
        }
    }
    return started;
}

// ----------------------------------------------------------------------------
// private methods

void CommCheckHandlerBase::communication_check_handler() {
    auto timeout_sub_type = init_sub_type;
    std::unique_lock lock(state_mutex);
    while (check_active.load()) {
        state_cv.wait_for(lock, timeout, [this] { return shutting_down or comm_check_value.has_value(); });
        if (shutting_down) {
            break;
        }
        const auto check = std::exchange(comm_check_value, std::nullopt);
        lock.unlock();
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
        lock.lock();
    }
}

} // namespace everest::lib::API
