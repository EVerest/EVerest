// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <utility>

#include <everest/util/async/monitor.hpp>
#include <generated/types/evse_board_support.hpp>

namespace module {

/// Hardware capabilities the BSP may deliver before ready() can accept them.
class BspCapabilitiesStore {
public:
    explicit BspCapabilitiesStore(types::evse_board_support::HardwareCapabilities defaults) : state(State{defaults}) {
    }

    /// Lets apply_when_allowed() store capabilities from now on, and wakes any call waiting in it.
    void allow_updates() {
        {
            auto handle = state.handle();
            handle->updates_allowed = true;
        }
        state.notify_all();
    }

    /// Stores caps. Blocks until allow_updates() has run.
    void apply_when_allowed(const types::evse_board_support::HardwareCapabilities& caps) {
        auto handle = state.handle();
        handle.wait([&handle]() { return handle->updates_allowed; });
        handle->caps = caps;
    }

    /// The capabilities last stored, or the defaults.
    types::evse_board_support::HardwareCapabilities get() {
        return state.handle()->caps;
    }

    /// Runs f(caps&) under the lock and returns its result.
    template <typename F> auto modify(F&& f) {
        auto handle = state.handle();
        return std::forward<F>(f)(handle->caps);
    }

private:
    struct State {
        types::evse_board_support::HardwareCapabilities caps;
        bool updates_allowed{false};
    };
    everest::lib::util::monitor<State> state;
};

} // namespace module
