// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "StateBase.hpp"

#include "FsmContext.hpp"

namespace module {

void StateBase::leave() {
    ctx.cancel_timer();
}

} // namespace module
