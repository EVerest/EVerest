// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once
#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/timer.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace everest::lib::slac {

class slac_fsm {
public:
    slac_fsm(fsm::evse::Context& ctx);
    ~slac_fsm();
    slac_fsm(slac_fsm const&) = delete;
    slac_fsm& operator=(slac_fsm const&) = delete;
    slac_fsm(slac_fsm&&) = delete;
    slac_fsm& operator=(slac_fsm&&) = delete;
    void reset();
    void enter_bcd();
    void leave_bcd();
    void message(messages::HomeplugMessage msg);
    void update();

    void restart_fsm();

    // Time until the machine next has something to do, measured from the last event; nothing while
    // it only waits for input.
    [[nodiscard]] std::optional<timer::tick> next_wakeup() const;

private:
    void begin_event();
    void settle();
    void event_post_processing();
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::vector<int> last_signature;
    fsm::evse::Context& ctx;
};

} // namespace everest::lib::slac
