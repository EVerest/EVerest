// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/io/event/fd_event_handler.hpp>

namespace iso15118::ev {

class EvController {

public:
    EvController() = default;
    ~EvController() = default;

    void start_session();

    // void send_control_event();
    // updating functions

private:
    everest::lib::io::event::fd_event_handler ev_handler; // poll_manager

    // session
    // config
};

} // namespace iso15118::ev
