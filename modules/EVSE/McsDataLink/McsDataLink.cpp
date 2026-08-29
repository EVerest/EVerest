// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "McsDataLink.hpp"

namespace module {

void McsDataLink::init() {
    invoke_init(*p_main);
}

void McsDataLink::ready() {
    invoke_ready(*p_main);
}

void McsDataLink::shutdown() {
    invoke_shutdown(*p_main);
}

} // namespace module
