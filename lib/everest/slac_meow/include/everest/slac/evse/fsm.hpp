// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>

#include <everest/slac/evse/context.hpp>
#include <everest/slac/protocol/homeplug_message.hpp>

namespace everest::slac::evse {

// Neither copyable nor movable: the states hold a reference to the context, so moving the facade
// would dangle them.
class EvseFSM {
public:
    explicit EvseFSM(Context& ctx);
    ~EvseFSM();

    EvseFSM(EvseFSM const&) = delete;
    EvseFSM& operator=(EvseFSM const&) = delete;
    EvseFSM(EvseFSM&&) = delete;
    EvseFSM& operator=(EvseFSM&&) = delete;

    /// Events fed before this are ignored.
    void restart_fsm();

    void reset();
    void enter_bcd();
    void leave_bcd();

    /// CM_VALIDATE reads the delta over its observation window.
    void count_bc(int count);
    void message(messages::HomeplugMessage msg);
    void update();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace everest::slac::evse
