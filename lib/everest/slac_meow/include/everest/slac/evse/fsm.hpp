// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>

#include <everest/slac/evse/context.hpp>
#include <everest/slac/protocol/homeplug_message.hpp>

namespace everest::slac::evse {

// Neither copyable nor movable: the states hold a reference to the context, so moving the facade
// would dangle them.
class FSM {
public:
    explicit FSM(Context& ctx);
    ~FSM();

    FSM(FSM const&) = delete;
    FSM& operator=(FSM const&) = delete;
    FSM(FSM&&) = delete;
    FSM& operator=(FSM&&) = delete;

    /// Events fed before this are ignored.
    void restart_fsm();

    void reset();
    void enter_bcd();
    void leave_bcd();
    void message(messages::HomeplugMessage msg);
    void update();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace everest::slac::evse
