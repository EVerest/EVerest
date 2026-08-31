// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>

#include <everest/slac/ev/context.hpp>
#include <everest/slac/protocol/homeplug_message.hpp>

namespace everest::slac::ev {

// Neither copyable nor movable: the states hold a reference to the context, so moving the facade
// would dangle them.
class EvFSM {
public:
    /// The context carries the callbacks, the configuration and the time source.
    explicit EvFSM(Context& ctx);
    ~EvFSM();

    EvFSM(EvFSM const&) = delete;
    EvFSM& operator=(EvFSM const&) = delete;
    EvFSM(EvFSM&&) = delete;
    EvFSM& operator=(EvFSM&&) = delete;

    /// Start the machine. Events fed before this are ignored.
    void restart_fsm();

    void reset();
    void trigger_matching();
    void message(messages::HomeplugMessage msg);
    void update();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace everest::slac::ev
