// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include <framework/ModuleAdapter.hpp>

#include "ISO15118_evImpl.hpp"

// What the tests that drive an Ev15118 module through its ISO15118_ev implementation share.
namespace module_driver {

// The command handlers the tests call, made public.
class TestImpl : public module::ev::ISO15118_evImpl {
public:
    using ISO15118_evImpl::handle_abort_charging;
    using ISO15118_evImpl::handle_cp_state_changed;
    using ISO15118_evImpl::handle_pause_charging;
    using ISO15118_evImpl::handle_set_bpt_dc_params;
    using ISO15118_evImpl::handle_set_dc_params;
    using ISO15118_evImpl::handle_start_charging;
    using ISO15118_evImpl::handle_stop_charging;
    using ISO15118_evImpl::handle_update_present_values;
    using ISO15118_evImpl::ISO15118_evImpl;
};

// ModuleBase is the framework's only door to an implementation's init() and ready().
class Driver : public Everest::ModuleBase {
public:
    using ModuleBase::ModuleBase;
    void init(Everest::ImplementationBase& impl) {
        invoke_init(impl);
    }
    void ready(Everest::ImplementationBase& impl) {
        invoke_ready(impl);
    }
};

// Answers the framework's error and mapping lookups with nothing; publish stays the test's.
inline void stub_lookups(Everest::ModuleAdapter& adapter) {
    adapter.get_error_manager_impl = [](const std::string&) { return nullptr; };
    adapter.get_error_state_monitor_impl = [](const std::string&) { return nullptr; };
    adapter.get_error_factory = [](const std::string&) { return nullptr; };
    adapter.get_mapping = []() { return std::nullopt; };
}

// A complete set of vehicle DC limits, as EvManager sends from its config in ready(); a DC start
// is rejected without them.
inline types::iso15118::DcEvParameters sane_dc_params() {
    types::iso15118::DcEvParameters params;
    params.max_power_limit = 150000;
    params.max_current_limit = 300;
    params.max_voltage_limit = 900;
    params.min_voltage_limit = 200;
    params.energy_capacity = 60000;
    params.target_voltage = 400;
    params.target_current = 100;
    return params;
}

inline void set_sane_dc_params(TestImpl& impl) {
    auto params = sane_dc_params();
    impl.handle_set_dc_params(params);
}

constexpr auto deadline = std::chrono::seconds(5);

// Polls `condition` until it holds, or false once the deadline passes.
inline bool eventually(const std::function<bool()>& condition) {
    const auto until = std::chrono::steady_clock::now() + deadline;
    while (not condition()) {
        if (std::chrono::steady_clock::now() > until) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

} // namespace module_driver
