// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/fsm_base.hpp>

namespace iso15118::ev {

// Result of one FSM feed as the Session sees it.
template <typename StateIdT> struct FeedOutcome {
    Disposition output{Disposition::Ignored};
    bool transitioned{false};
    StateIdT state_before{};
    // Declared disposition not matched by what the state did; nullptr when consistent.
    const char* violation{nullptr};
};

} // namespace iso15118::ev
