// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "EvIso15118D20.hpp"

namespace module {

void EvIso15118D20::init() {
    invoke_init(*p_ev);
}

void EvIso15118D20::ready() {
    invoke_ready(*p_ev);
}

} // namespace module
