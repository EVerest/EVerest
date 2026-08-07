// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

namespace everest::lib::io::event {

class fd_event_handler;

/**
 * @brief Handler-side block a registration is recorded against
 * @details The handler owns the block and clears \p handler in its destructor body, before its own
 * members are destroyed. A recorder holds a \p std::weak_ptr to it, so teardown after the handler's
 * death has no pointer left to dereference. Deliberately includes nothing, so recorders can read it
 * without depending on fd_event_handler.hpp.
 */
struct handler_liveness {
    fd_event_handler* handler{nullptr};
};

} // namespace everest::lib::io::event
