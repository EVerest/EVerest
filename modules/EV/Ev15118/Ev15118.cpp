// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Ev15118.hpp"

namespace module {

void Ev15118::init() {
    invoke_init(*p_ev);
}

void Ev15118::ready() {
    invoke_ready(*p_ev);
}

} // namespace module
