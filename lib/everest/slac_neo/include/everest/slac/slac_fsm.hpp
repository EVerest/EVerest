// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once
#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/fsm/evse/context.hpp>

namespace everest::lib::slac {

class slac_fsm {
public:
    slac_fsm(fsm::evse::Context& ctx);
    ~slac_fsm();
    void reset();
    void enter_bcd();
    void leave_bcd();
    void message(messages::HomeplugMessage msg);
    void update();

    void restart_fsm();

private:
    void event_post_processing();
    void* fsm;
    std::vector<int> last_signature;
    fsm::evse::Context& ctx;
};

} // namespace everest::lib::slac
