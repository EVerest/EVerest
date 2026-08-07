// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Eastron_SDM630EV.hpp"

namespace module {

void Eastron_SDM630EV::init() {
    invoke_init(*p_main);
}

void Eastron_SDM630EV::ready() {
    invoke_ready(*p_main);
}

void Eastron_SDM630EV::shutdown() {
    invoke_shutdown(*p_main);
}

} // namespace module
