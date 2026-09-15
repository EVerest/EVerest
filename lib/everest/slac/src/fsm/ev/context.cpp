// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#include <everest/slac/fsm/ev/context.hpp>

namespace everest::lib::slac::fsm::ev {

void Context::sample_time() {
    current_time = callbacks.now ? callbacks.now() : timer::clock::now();
}

} // namespace everest::lib::slac::fsm::ev
