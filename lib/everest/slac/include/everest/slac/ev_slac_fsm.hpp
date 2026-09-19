// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once
#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/fsm/ev/context.hpp>
#include <memory>

namespace everest::lib::slac {

class ev_slac_fsm {
public:
    ev_slac_fsm(fsm::ev::Context& ctx);
    ~ev_slac_fsm();
    ev_slac_fsm(ev_slac_fsm const&) = delete;
    ev_slac_fsm& operator=(ev_slac_fsm const&) = delete;
    ev_slac_fsm(ev_slac_fsm&&) = delete;
    ev_slac_fsm& operator=(ev_slac_fsm&&) = delete;
    void reset();
    void trigger_matching();
    void message(messages::HomeplugMessage msg);
    void update();

    void restart_fsm();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    fsm::ev::Context& ctx;
};

} // namespace everest::lib::slac
